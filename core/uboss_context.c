/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Context
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"

#include "uboss_context.h"
#include "uboss_server.h"
#include "uboss_module.h"
#include "uboss_handle.h"
#include "uboss_monitor.h"
#include "uboss_mq.h"
#include "uboss_log.h"
#include "uboss_start.h"
#include "uboss_lock.h"
#include "uboss_timer.h"
#include "uboss_atomic.h"

#include <pthread.h>

#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

// 节点总数
int
uboss_context_total() {
	return G_NODE.total;
}

// 节点加一
static void
context_inc() {
	ATOM_INC(&G_NODE.total);
}

// 节点减一
static void
context_dec() {
	ATOM_DEC(&G_NODE.total);
}

//
struct drop_t {
	uint32_t handle;
};

// 退出消息
static void
drop_message(struct uboss_message *msg, void *ud) {
	struct drop_t *d = ud;
	uboss_free(msg->data); // 释放消息的数据内存空间
	uint32_t source = d->handle;
	assert(source);
	// report error to the message source
	uboss_send(NULL, source, msg->source, PTYPE_ERROR, 0, NULL, 0); // 发送消息的来源地址
}

// 新建 uBoss 的上下文
struct uboss_context *
uboss_context_new(const char * name, const char *param) {
	struct uboss_module * mod = uboss_module_query(name); // 根据模块名，查找模块数组中模块的指针

	if (mod == NULL) // 如果指针为空
		return NULL; // 直接返回

	// 找到模块，则取出 创建函数 的指针
	// 因为在uboss_module中create函数不存在时，返回值为：
	// (intptr_t)(~0); 即0xFFFFFFFF（32位地址时）
	// 所以如果获取的函数为 NULL = 0 时，即表示错误。
	void *inst = uboss_module_instance_create(mod);
	if (inst == NULL) // 如果指针为空
		return NULL; // 直接返回

	// 分配 uBoss 结构的内存空间
	struct uboss_context * ctx = uboss_malloc(sizeof(*ctx));
	CHECKCALLING_INIT(ctx)

	ctx->mod = mod; // 模块的指针
	ctx->instance = inst; // 模块的创建函数的指针
	ctx->ref = 2; // 模块的消息队列槽的大小值
	ctx->cb = NULL; // 模块的回调函数的指针
	ctx->cb_ud = NULL; // 用户数据的指针
	ctx->session_id = 0; // 服务的会话ID
	ctx->logfile = NULL; // 是否开启日志记录到文件功能

	ctx->init = false; // 是否初始化
	ctx->endless = false; // 是否进入了无限循环

	// 应该先设置句柄为0,以避免 uboss_handle_retireall 回收时获得一个为初始化的句柄。
	// Should set to 0 first to avoid uboss_handle_retireall get an uninitialized handle
	ctx->handle = 0; // 先赋值为0，分配内存空间
	ctx->handle = uboss_handle_register(ctx); // 再注册到句柄注册中心，获得句柄值

	// 初始化函数也许需要使用 ctx->handle ，因为它必须在最后。
	// init function maybe use ctx->handle, so it must init at last
	struct message_queue * queue = ctx->queue = uboss_mq_create(ctx->handle); // 创建消息队列

	context_inc(); // 上下文 加一

	CHECKCALLING_BEGIN(ctx)
	int r = uboss_module_instance_init(mod, inst, ctx, param); // 从模块指针中获取 初始化函数的指针（模块中必须有的函数）
	CHECKCALLING_END(ctx)
	if (r == 0) { // 初始化函数返回值为0，表示正常。
		struct uboss_context * ret = uboss_context_release(ctx); // 检查上下文释放标志 ref 是否为0
		if (ret) {
			ctx->init = true; // 设置服务的上下文结构，初始化成功。
		}
		uboss_globalmq_push(queue); // 将消息压入全局消息队列
		if (ret) {
			uboss_error(ret, "LAUNCH %s %s", name, param ? param : "");
		}
		return ret;
	} else { // 返回错误
		uboss_error(ctx, "FAILED launch %s", name);
		uint32_t handle = ctx->handle;
		uboss_context_release(ctx);
		uboss_handle_retire(handle);
		struct drop_t d = { handle };
		uboss_mq_release(queue, drop_message, &d);
		return NULL;
	}
}

// 新建会话
int
uboss_context_newsession(struct uboss_context *ctx) {
	// session always be a positive number
	int session = ++ctx->session_id;
	if (session <= 0) { // 如果会话ID小于等于0
		ctx->session_id = 1; // 会话ID等于1
		return 1;
	}
	return session;
}

void
uboss_context_grab(struct uboss_context *ctx) {
	ATOM_INC(&ctx->ref); // 原子操作 调用次数 加一
}

void
uboss_context_reserve(struct uboss_context *ctx) {
	uboss_context_grab(ctx);
	// don't count the context reserved, because uboss abort (the worker threads terminate) only when the total context is 0 .
	// the reserved context will be release at last.
	context_dec(); // 上下文结构数量减一
}

// 删除上下文结构
static void
delete_context(struct uboss_context *ctx) {
	// 如果启动了日志文件
	if (ctx->logfile) {
		fclose(ctx->logfile); // 关闭日志句柄
	}
	uboss_module_instance_release(ctx->mod, ctx->instance); // 调用模块中的释放函数
	uboss_mq_mark_release(ctx->queue); // 标记 消息队列 可以释放
	CHECKCALLING_DESTROY(ctx) // 检查并销毁 上下文结构
	uboss_free(ctx); // 释放上下文结构
	context_dec(); // 上下文结构数量减一
}

// 释放上下文
struct uboss_context *
uboss_context_release(struct uboss_context *ctx) {
	// 如果上下文结构数量减一，等于0
	if (ATOM_DEC(&ctx->ref) == 0) {
		delete_context(ctx); // 删除上下文结构
		return NULL;
	}
	return ctx;
}

// 将上下文压入消息队列
int
uboss_context_push(uint32_t handle, struct uboss_message *message) {
	struct uboss_context * ctx = uboss_handle_grab(handle);
	if (ctx == NULL) {
		return -1;
	}
	uboss_mq_push(ctx->queue, message); // 将消息压入消息队列
	uboss_context_release(ctx); // 释放上下文

	return 0;
}

void
uboss_context_endless(uint32_t handle) {
	struct uboss_context * ctx = uboss_handle_grab(handle);
	if (ctx == NULL) {
		return;
	}
	ctx->endless = true;
	uboss_context_release(ctx);
}

// 返回上下文结构中的句柄值
uint32_t
uboss_context_handle(struct uboss_context *ctx) {
	return ctx->handle;
}

// 分发消息
static void
dispatch_message(struct uboss_context *ctx, struct uboss_message *msg) {
	assert(ctx->init);
	CHECKCALLING_BEGIN(ctx)
	pthread_setspecific(G_NODE.handle_key, (void *)(uintptr_t)(ctx->handle)); // 设置线程通道
	int type = msg->sz >> MESSAGE_TYPE_SHIFT; // 获得消息的类型
	size_t sz = msg->sz & MESSAGE_TYPE_MASK; // 获得消息的长度
	if (ctx->logfile) { // 如果uboss上下文设置类日志文件
		uboss_log_output(ctx->logfile, msg->source, type, msg->session, msg->data, sz); // 写入日志信息
	}
	++ctx->message_count;

	int reserve_msg;
	if (ctx->profile) {
		// 这里获得线程开始的时间，然后执行返回函数，再调用时间
		// 最后计算整个服务中的返回函数一共执行了多少时间。
		ctx->cpu_start = uboss_thread_time(); // 获得开始时间

		// 调用 服务模块中的返回函数
		reserve_msg = ctx->cb(ctx, ctx->cb_ud, type, msg->session, msg->source, msg->data, sz);
		uint64_t cost_time = uboss_thread_time() - ctx->cpu_start; // 获得结束时间，并计算消耗时间
		ctx->cpu_cost += cost_time; // 累计消耗的时间
	} else {
		// 核心功能：调用服务中定义的返回函数
		// 调用 服务模块中的返回函数
		reserve_msg = ctx->cb(ctx, ctx->cb_ud, type, msg->session, msg->source, msg->data, sz);
	}
	if (!reserve_msg) {
		uboss_free(msg->data); // 释放消息数据
	}
	CHECKCALLING_END(ctx)
}


// 分发所有消息
void
uboss_context_dispatchall(struct uboss_context * ctx) {
	// for uboss_error
	struct uboss_message msg;
	struct message_queue *q = ctx->queue; // 取出上下文中的消息队列
	while (!uboss_mq_pop(q,&msg)) { // 弹出消息队列中的消息
		dispatch_message(ctx, &msg); // 将消息分发出去
	}
}

// 框架分发消息
struct message_queue *
uboss_context_message_dispatch(struct uboss_monitor *sm, struct message_queue *q, int weight) {
	// 如果传入的消息队列地址为空值
	if (q == NULL) {
		q = uboss_globalmq_pop(); // 弹出全局消息
		if (q==NULL)
			return NULL;
	}

	uint32_t handle = uboss_mq_handle(q); // 从队列中取出对应的句柄值

	struct uboss_context * ctx = uboss_handle_grab(handle); // 根据 句柄值 获得上下文结构的指针
	if (ctx == NULL) {
		struct drop_t d = { handle };
		uboss_mq_release(q, drop_message, &d); // 释放消息队列
		return uboss_globalmq_pop(); // 弹出全局消息队列
	}

	int i,n=1;
	struct uboss_message msg;

	for (i=0;i<n;i++) {
		if (uboss_mq_pop(q,&msg)) { // 弹出uboss消息
			uboss_context_release(ctx); // 释放上下文
			return uboss_globalmq_pop(); // 从全局队列中弹出消息
		} else if (i==0 && weight >= 0) {
			n = uboss_mq_length(q); // 获得队列的长度
			n >>= weight; // 权重
		}

		// 服务消息队列调度时，如果发现过载，则打印输出过载信息
		// 过载后框架不会做其他处理，仅输出消息
		int overload = uboss_mq_overload(q); // 消息过载
		if (overload) {
			uboss_error(ctx, "May overload, message queue length = %d", overload);
		}


		//
		// 在处理这条消息前触发监视，给这个线程的版本加一
		// 并将消息来源和处理的服务地址发给监视线程。
		//
		uboss_monitor_trigger(sm, msg.source , handle); // 触发监视

		// 如果上下文中的返回函数指针为空
		if (ctx->cb == NULL) {
			uboss_free(msg.data); // 释放消息的数据内存空间
		} else {
			dispatch_message(ctx, &msg); // 核心功能：分发消息
		}

		//
		// 处理完消息后，再触发监视，将来源和目的设置成框架自己
		// 以做好下次调用准备，即清空来源和目的地址。
		//
		uboss_monitor_trigger(sm, 0,0); // 触发监视
	}

	assert(q == ctx->queue);
	struct message_queue *nq = uboss_globalmq_pop();
	if (nq) {
		// 如果全局消息队列不为空，压入q后，并返回下一个消息队列 nq
		// If global mq is not empty , push q back, and return next queue (nq)
		// 否则，全局消息队列为空或则阻塞，不压入q后，并返回 q，再一次调度
		// Else (global mq is empty or block, don't push q back, and return q again (for next dispatch)
		uboss_globalmq_push(q);
		q = nq;
	}
	uboss_context_release(ctx);

	return q;
}

// 发送消息
void
uboss_context_send(struct uboss_context * ctx, void * msg, size_t sz, uint32_t source, int type, int session) {
	struct uboss_message smsg; // 声明消息结构
	smsg.source = source; // 来源地址
	smsg.session = session; // 目的地址
	smsg.data = msg; // 消息的指针
	smsg.sz = sz | (size_t)type << MESSAGE_TYPE_SHIFT; // 消息的长度

	uboss_mq_push(ctx->queue, &smsg); // 将消息压入消息队列
}
