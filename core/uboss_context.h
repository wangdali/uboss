/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Context
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#ifndef UBOSS_CONTEXT_H
#define UBOSS_CONTEXT_H

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef CALLING_CHECK

#define CHECKCALLING_BEGIN(ctx) if (!(spinlock_trylock(&ctx->calling))) { assert(0); }
#define CHECKCALLING_END(ctx) spinlock_unlock(&ctx->calling);
#define CHECKCALLING_INIT(ctx) spinlock_init(&ctx->calling);
#define CHECKCALLING_DESTROY(ctx) spinlock_destroy(&ctx->calling);
#define CHECKCALLING_DECL struct spinlock calling;

#else

#define CHECKCALLING_BEGIN(ctx)
#define CHECKCALLING_END(ctx)
#define CHECKCALLING_INIT(ctx)
#define CHECKCALLING_DESTROY(ctx)
#define CHECKCALLING_DECL

#endif

// uBoss 上下文结构
struct uboss_context {
	void * instance; // 实例
	struct uboss_module * mod; // 模块
	void * cb_ud; // 用户数据的指针
	uboss_cb cb; // 服务模块的返回函数指针
	struct message_queue *queue; // 消息队列
	FILE * logfile; // 日志文件流
	uint64_t cpu_cost;	// in microsec
	uint64_t cpu_start;	// in microsec
	char result[32]; // 结果
	uint32_t handle; // 句柄值
	int session_id; // 会话 ID
	int ref; // 调用次数
	int message_count;
	bool init; // 初始化
	bool endless; // 终结标志
	bool profile;

	CHECKCALLING_DECL
};

struct uboss_message;
struct uboss_monitor;

struct uboss_context * uboss_context_new(const char * name, const char * parm);
void uboss_context_grab(struct uboss_context *);
void uboss_context_reserve(struct uboss_context *ctx);
struct uboss_context * uboss_context_release(struct uboss_context *);
uint32_t uboss_context_handle(struct uboss_context *);
int uboss_context_push(uint32_t handle, struct uboss_message *message);
void uboss_context_send(struct uboss_context * context, void * msg, size_t sz, uint32_t source, int type, int session);
int uboss_context_newsession(struct uboss_context *);
struct message_queue * uboss_context_message_dispatch(struct uboss_monitor *, struct message_queue *, int weight);	// return next queue
int uboss_context_total();
void uboss_context_dispatchall(struct uboss_context * context);	// for uboss_error output before exit

void uboss_context_endless(uint32_t handle);	// for monitor


#endif /* UBOSS_CONTEXT_H */
