/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Message Queue
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#ifndef UBOSS_MQ_H
#define UBOSS_MQ_H

#include <stdlib.h>
#include <stdint.h>

// uboss 消息
struct uboss_message {
	uint32_t source; // 来源
	int session; // 会话
	void * data; // 数据的地址
	size_t sz; // 数据的长度(前8bit表示数据的类型)
};

// 数据类型：uboss_message.sz 高 8bit
#define MESSAGE_TYPE_MASK (SIZE_MAX >> 8)
#define MESSAGE_TYPE_SHIFT ((sizeof(size_t)-1) * 8)

struct message_queue;

void uboss_globalmq_push(struct message_queue * queue);
struct message_queue * uboss_globalmq_pop(void);

struct message_queue * uboss_mq_create(uint32_t handle);
void uboss_mq_mark_release(struct message_queue *q);

typedef void (*message_drop)(struct uboss_message *, void *);

void uboss_mq_release(struct message_queue *q, message_drop drop_func, void *ud);
uint32_t uboss_mq_handle(struct message_queue *q);

// 0 for success
int uboss_mq_pop(struct message_queue *q, struct uboss_message *message);
void uboss_mq_push(struct message_queue *q, struct uboss_message *message);

// return the length of message queue, for debug
int uboss_mq_length(struct message_queue *q);
int uboss_mq_overload(struct message_queue *q);

void uboss_mq_init();

#endif /* UBOSS_MQ_H */
