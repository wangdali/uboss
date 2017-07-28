/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Message Queue
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"
#include "uboss_mq.h"
#include "uboss_lock.h"
#include "uboss_handle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define DEFAULT_QUEUE_SIZE 64 // 默认队列大小
//#define MAX_GLOBAL_MQ 0x10000 // 全局消息队列最大值

// 0 表示消息队列不在全局队列中
// 1 表示消息队列在全局队列中，或者消息在调度中

#define MQ_IN_GLOBAL 1
#define MQ_OVERLOAD 1024

// 消息队列的结构
struct message_queue {
	struct spinlock lock; // 锁
	uint32_t handle; // 句柄值
	int cap; // 链表数量
	int head; // 链表头
	int tail; // 链表尾
	int release; // 是否可释放
	int in_global; // 是否在全局队列
	int overload; // 过载
	int overload_threshold; // 过载阀值
	struct uboss_message *queue;  // 消息队列的指针
	struct message_queue *next;  // 下一个消息队列的指针
};

// 全局消息队列的结构
struct global_queue {
	struct message_queue *head; // 消息队列的链表头
	struct message_queue *tail; // 消息队列的链表尾
	struct spinlock lock; // 锁
};

static struct global_queue *Q = NULL; // 声明全局队列

// 将服务的消息队列压入全局消息队列
void
uboss_globalmq_push(struct message_queue * queue) {
	struct global_queue *q= Q; // 获取 全局消息队列

	SPIN_LOCK(q) // 锁住

	// 断言，服务消息的next必须为NULL，否在结束。
	// 因为不能把一个已存在消息队列的消息存入，否在会有意想不到的错误。
	assert(queue->next == NULL);

	// 如果链表尾存在数据，表示全局队列不为空
	if(q->tail) {
		// 在全局队列中找到尾部的服务，并把服务的消息队列的next指向queue
		q->tail->next = queue;

		// 把消息插入服务的消息队列尾部
		q->tail = queue;
	} else {
		// 全局队列为空时，压入第一个服务队列
		q->head = q->tail = queue; // 头尾都为这个队列
	}
	SPIN_UNLOCK(q) // 解锁
}

// 从全局消息队列中弹出服务的消息队列
struct message_queue *
uboss_globalmq_pop() {
	struct global_queue *q = Q; // 声明全局消息队列

	SPIN_LOCK(q) // 锁住
	struct message_queue *mq = q->head; // 从队列头弹出消息队列

	// 如果消息队列存在
	if(mq) {
		q->head = mq->next; // 全局队列的头 = 下一个消息队列
		// 如果全局队列的头等于空
		if(q->head == NULL) {
			assert(mq == q->tail); // 断言，即全局队列的头部和尾部相同
			q->tail = NULL; // 设置消息队列尾也等于空
		}
		mq->next = NULL; // 设置取出的消息队列的下一个消息队列为空
	}
	SPIN_UNLOCK(q) // 解锁

	return mq; // 返回消息队列
}

// 创建服务的消息队列
struct message_queue *
uboss_mq_create(uint32_t handle) {
	struct message_queue *q = uboss_malloc(sizeof(*q)); // 分配消息队列的内存空间
	q->handle = handle; // 句柄值
	q->cap = DEFAULT_QUEUE_SIZE; // 默认队列大小 =64
	q->head = 0; // 链表的头
	q->tail = 0; // 链表的尾
	SPIN_INIT(q) // 初始化锁
	// When the queue is create (always between service create and service init) ,
	// set in_global flag to avoid push it to global queue .
	// If the service init success, uboss_context_new will call uboss_mq_push to push it to global queue.
	q->in_global = MQ_IN_GLOBAL; // 是否在全局队列中：0=否 1=是 （默认为1）
	q->release = 0; // 是否可释放：0=否 1=是
	q->overload = 0; // 是否过载：0=否 1=是
	q->overload_threshold = MQ_OVERLOAD; // 过载的阀值 （默认为1024）
	q->queue = uboss_malloc(sizeof(struct uboss_message) * q->cap); // 分配队列的内存空间
	q->next = NULL; // 下一个队列的指针

	return q;
}

// 释放
static void
_release(struct message_queue *q) {
	assert(q->next == NULL);
	SPIN_DESTROY(q) // 销毁锁
	uboss_free(q->queue); // 释放队列
	uboss_free(q); // 释放消息
}

// 从服务的消息队列中获取句柄的值
uint32_t
uboss_mq_handle(struct message_queue *q) {
	return q->handle; // 返回句柄
}

// 获得消息队列的长度
int
uboss_mq_length(struct message_queue *q) {
	int head, tail,cap;

	SPIN_LOCK(q) // 锁住
	head = q->head; // 获得队列头
	tail = q->tail; // 获得队列尾
	cap = q->cap; // 获得队列的长度
	SPIN_UNLOCK(q) // 解锁

	// 如果 队列头 <= 队列尾
	if (head <= tail) {
		return tail - head; // 队列尾 - 队列头
	}
	return tail + cap - head; // 队列尾 + 队列长度 - 队列头
}

// 消息队列过载
int
uboss_mq_overload(struct message_queue *q) {
	if (q->overload) {
		int overload = q->overload;
		q->overload = 0; // 设置过载为零
		return overload;
	}
	return 0;
}

// 从服务消息队列中弹出消息
int
uboss_mq_pop(struct message_queue *q, struct uboss_message *message) {
	int ret = 1;
	SPIN_LOCK(q) // 锁住

	// 如果队列头不等于队列尾
	if (q->head != q->tail) {
		*message = q->queue[q->head++]; // 从消息队列头取出消息后 +1
		ret = 0;
		int head = q->head; // 获得队列头
		int tail = q->tail; // 获得队列尾
		int cap = q->cap; // 获得队列长度

		// 队列头 大于等于 队列数量，表示达到容器尾，应容器头开始
		if (head >= cap) {
			q->head = head = 0; // 队列头 =0
		}
		int length = tail - head; // 计算队列长度
		if (length < 0) {
			length += cap;
		}

		// 如果队列长度 大于 过载阀值，阀值放大2倍
		while (length > q->overload_threshold) {
			q->overload = length; // 过载值 = 队列长度
			q->overload_threshold *= 2; // 过载阀值放大2倍
		}
	} else {
		// 服务消息队列为空时，重置 过载阀值 为默认值
		// 当消息队列为空时，是否也应该重置一下 消息队列 为默认值
		q->overload_threshold = MQ_OVERLOAD; // 否则过载阀值为默认 1024
	}

	if (ret) {
		q->in_global = 0;
	}

	SPIN_UNLOCK(q) // 解锁

	return ret;
}

// 展开队列
static void
expand_queue(struct message_queue *q) {
	struct uboss_message *new_queue = uboss_malloc(sizeof(struct uboss_message) * q->cap * 2); // 分配新队列的内存空间
	int i;

	// 循环所有消息队列
	for (i=0;i<q->cap;i++) {
		new_queue[i] = q->queue[(q->head + i) % q->cap]; // 将消息队列赋值给新队列
	}
	q->head = 0;
	q->tail = q->cap;
	q->cap *= 2; // 队列长度发大 2 倍

	uboss_free(q->queue); // 释放旧队列
	q->queue = new_queue; // 将新队列替换旧队列
}

// 将消息压入服务的消息队列中
void
uboss_mq_push(struct message_queue *q, struct uboss_message *message) {
	assert(message);
	SPIN_LOCK(q) // 锁住

	q->queue[q->tail] = *message; // 消息赋给队列尾

	// 如果队列尾+1 大于等于 队列数量时，队列尾 =0
	if (++ q->tail >= q->cap) {
		q->tail = 0; // 循环到队列头
	}

	// 如果队列头 等于 队列尾，则表示队列已满，需要展开2倍
	if (q->head == q->tail) {
		expand_queue(q); // 展开（成倍放大）队列
	}

	// 如果服务队列不在全局队列中
	if (q->in_global == 0) {
		q->in_global = MQ_IN_GLOBAL; // 设置队列在全局队列中
		uboss_globalmq_push(q); // 将队列压入全局队列
	}

	SPIN_UNLOCK(q) // 解锁
}

// 标志消息队列可释放
void
uboss_mq_mark_release(struct message_queue *q) {
	SPIN_LOCK(q) // 锁住
	assert(q->release == 0); // 断言
	q->release = 1; // 标志释放

	// 如果队列不在全局队列中
	if (q->in_global != MQ_IN_GLOBAL) {
		uboss_globalmq_push(q); // 压入全局队列
	}
	SPIN_UNLOCK(q) // 解锁
}

// 放弃队列
static void
_drop_queue(struct message_queue *q, message_drop drop_func, void *ud) {
	struct uboss_message msg;
	while(!uboss_mq_pop(q, &msg)) {
		drop_func(&msg, ud);
	}
	_release(q);
}

// 释放队列
void
uboss_mq_release(struct message_queue *q, message_drop drop_func, void *ud) {
	SPIN_LOCK(q) // 锁住

	if (q->release) {
		SPIN_UNLOCK(q) // 解锁
		_drop_queue(q, drop_func, ud);
	} else {
		uboss_globalmq_push(q);
		SPIN_UNLOCK(q) // 解锁
	}
}

// 初始化服务的消息队列
void
uboss_mq_init() {
	struct global_queue *q = uboss_malloc(sizeof(*q)); // 声明一个全局队列的结构
	memset(q,0,sizeof(*q)); // 清空结构
	SPIN_INIT(q); // 初始化锁
	Q=q; // 将结构的指针赋给全局变量
}
