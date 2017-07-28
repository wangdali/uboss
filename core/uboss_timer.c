/*
** Copyright (c) 2014-2016 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Timer
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"

#include "uboss_timer.h"
#include "uboss_mq.h"
#include "uboss_server.h"
#include "uboss_handle.h"
#include "uboss_lock.h"

#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#if defined(__APPLE__)
#include <sys/time.h>
#endif

// 定时器回调函数
typedef void (*timer_execute_func)(void *ud,void *arg);

#define TIME_NEAR_SHIFT 8
#define TIME_NEAR (1 << TIME_NEAR_SHIFT)
#define TIME_LEVEL_SHIFT 6
#define TIME_LEVEL (1 << TIME_LEVEL_SHIFT)
#define TIME_NEAR_MASK (TIME_NEAR-1)
#define TIME_LEVEL_MASK (TIME_LEVEL-1)

// 定时器事件
struct timer_event {
	uint32_t handle; // 句柄
	int session; // 会话
};

// 定时器节点
struct timer_node {
	struct timer_node *next; // 下一个定时器的节点
	uint32_t expire; // 期限
};

// 链表
struct link_list {
	struct timer_node head; // 链表头
	struct timer_node *tail; // 链表尾
};

// 定时器
struct timer {
	struct link_list near[TIME_NEAR];
	struct link_list t[4][TIME_LEVEL];
	struct spinlock lock; // 锁
	uint32_t time; // 时间
	uint32_t starttime; // 开始时间
	uint64_t current; // 当前
	uint64_t current_point; // 当前点
};

// 定时器结构
static struct timer * TI = NULL;

// 清理链表
static inline struct timer_node *
link_clear(struct link_list *list) {
	struct timer_node * ret = list->head.next;
	list->head.next = 0; // 设置下一个节点为空，即没有下个节点
	list->tail = &(list->head); // 将链表尾 = 链表头

	return ret;
}

// 链表
static inline void
link(struct link_list *list,struct timer_node *node) {
	list->tail->next = node; // 设置下一个节点的地址
	list->tail = node; // 链表尾 = 本节点
	node->next=0; // 下一个节点的下一个节点为哦嗯
}

// 添加节点
static void
add_node(struct timer *T,struct timer_node *node) {
	uint32_t time=node->expire; // 获得时间期限
	uint32_t current_time=T->time; // 设置当前时间

	if ((time|TIME_NEAR_MASK)==(current_time|TIME_NEAR_MASK)) {
		link(&T->near[time&TIME_NEAR_MASK],node);
	} else {
		int i;
		uint32_t mask=TIME_NEAR << TIME_LEVEL_SHIFT;
		for (i=0;i<3;i++) {
			if ((time|(mask-1))==(current_time|(mask-1))) {
				break;
			}
			mask <<= TIME_LEVEL_SHIFT;
		}

		link(&T->t[i][((time>>(TIME_NEAR_SHIFT + i*TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)],node);
	}
}

// 添加定时器
static void
timer_add(struct timer *T,void *arg,size_t sz,int time) {
	struct timer_node *node = (struct timer_node *)uboss_malloc(sizeof(*node)+sz); // 分配内存空间
	memcpy(node+1,arg,sz); // 复制参数

	SPIN_LOCK(T); // 锁

		node->expire=time+T->time; //
		add_node(T,node); // 添加节点

	SPIN_UNLOCK(T); // 解锁
}

// 移动列表
static void
move_list(struct timer *T, int level, int idx) {
	struct timer_node *current = link_clear(&T->t[level][idx]);
	while (current) {
		struct timer_node *temp=current->next;
		add_node(T,current);
		current=temp;
	}
}

static void
timer_shift(struct timer *T) {
	int mask = TIME_NEAR;
	uint32_t ct = ++T->time;
	if (ct == 0) {
		move_list(T, 3, 0);
	} else {
		uint32_t time = ct >> TIME_NEAR_SHIFT;
		int i=0;

		while ((ct & (mask-1))==0) {
			int idx=time & TIME_LEVEL_MASK;
			if (idx!=0) {
				move_list(T, i, idx);
				break;
			}
			mask <<= TIME_LEVEL_SHIFT;
			time >>= TIME_LEVEL_SHIFT;
			++i;
		}
	}
}

static inline void
dispatch_list(struct timer_node *current) {
	do {
		struct timer_event * event = (struct timer_event *)(current+1); // 定时器事件
		struct uboss_message message;
		message.source = 0; // 消息来源地址为自己
		message.session = event->session; // 事件的会话
		message.data = NULL; // 消息的数据
		message.sz = (size_t)PTYPE_RESPONSE << MESSAGE_TYPE_SHIFT; // 消息的数据长度

		// 将消息压入队列
		uboss_context_push(event->handle, &message);

		struct timer_node * temp = current; // 节点
		current=current->next;
		uboss_free(temp);
	} while (current);
}

// 执行定时器
static inline void
timer_execute(struct timer *T) {
	int idx = T->time & TIME_NEAR_MASK;

	while (T->near[idx].head.next) {
		struct timer_node *current = link_clear(&T->near[idx]);
		SPIN_UNLOCK(T);
		// dispatch_list don't need lock T
		dispatch_list(current);
		SPIN_LOCK(T);
	}
}

// 更新定时器
static void
timer_update(struct timer *T) {
	SPIN_LOCK(T);

	// try to dispatch timeout 0 (rare condition)
	timer_execute(T);

	// shift time first, and then dispatch timer message
	timer_shift(T);

	timer_execute(T);

	SPIN_UNLOCK(T);
}

// 创建定时器
static struct timer *
timer_create_timer() {
	struct timer *r=(struct timer *)uboss_malloc(sizeof(struct timer)); // 分配内存空间
	memset(r,0,sizeof(*r)); // 清空内存空间

	int i,j;

	for (i=0;i<TIME_NEAR;i++) {
		link_clear(&r->near[i]);
	}

	for (i=0;i<4;i++) {
		for (j=0;j<TIME_LEVEL;j++) {
			link_clear(&r->t[i][j]);
		}
	}

	SPIN_INIT(r)

	r->current = 0;

	return r;
}

// 超时
int
uboss_timeout(uint32_t handle, int time, int session) {
	if (time <= 0) {
		struct uboss_message message;
		message.source = 0; // 消息来源
		message.session = session; // 消息会话
		message.data = NULL; // 消息数据地址
		message.sz = (size_t)PTYPE_RESPONSE << MESSAGE_TYPE_SHIFT; // 消息的长度

		// 将消息压入
		if (uboss_context_push(handle, &message)) {
			return -1;
		}
	} else {
		struct timer_event event; // 定时器事件
		event.handle = handle; // 句柄
		event.session = session; // 会话
		timer_add(TI, &event, sizeof(event), time); // 加入定时器
	}

	return session; // 返回会话
}

// centisecond: 1/100 second
// 百分之一秒
static void
systime(uint32_t *sec, uint32_t *cs) {
#if !defined(__APPLE__)
	struct timespec ti;
	clock_gettime(CLOCK_REALTIME, &ti); // 获得系统实时的时间
	*sec = (uint32_t)ti.tv_sec; // 秒钟
	*cs = (uint32_t)(ti.tv_nsec / 10000000); // 纳秒 转1/00秒
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	*sec = tv.tv_sec;
	*cs = tv.tv_usec / 10000;
#endif
}

static uint64_t
gettime() {
	uint64_t t;
#if !defined(__APPLE__)
	struct timespec ti;
	clock_gettime(CLOCK_MONOTONIC, &ti); // 从系统启动开始的时间
	t = (uint64_t)ti.tv_sec * 100; // 秒 * 100 倍
	t += ti.tv_nsec / 10000000; // 纳秒 转1/100秒
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	t = (uint64_t)tv.tv_sec * 100;
	t += tv.tv_usec / 10000;
#endif
	return t; // 返回 100倍秒+百分之一秒
}

// 更新时间
void
uboss_updatetime(void) {
	uint64_t cp = gettime(); // 获得系统启动的时间点
	if(cp < TI->current_point) { // 如果 现在时间点 小于 之前的时间点，则错误
		uboss_error(NULL, "time diff error: change from %lld to %lld", cp, TI->current_point);
		TI->current_point = cp; // 将 现在时间点 替换 之前的时间点 的值
	} else if (cp != TI->current_point) { // 如果 现在时间点 不等于 之前的时间点
		uint32_t diff = (uint32_t)(cp - TI->current_point);
		TI->current_point = cp;
		TI->current += diff;
		int i;
		for (i=0;i<diff;i++) {
			timer_update(TI);
		}
	}
}

// 返回开始时间
uint32_t
uboss_starttime(void) {
	return TI->starttime;
}

// 返回当前时间
uint64_t
uboss_now(void) {
	return TI->current;
}

// 初始化定时器
void
uboss_timer_init(void) {
	TI = timer_create_timer(); // 创建定时器
	uint32_t current = 0;
	systime(&TI->starttime, &current); // 获得系统的实时时间，starttime为秒钟，current为百分之一秒
	TI->current = current; // 设置当前时间 1/100秒
	TI->current_point = gettime(); // 获得系统启动的时间点
}

// for profile

#define NANOSEC 1000000000
#define MICROSEC 1000000

uint64_t
uboss_thread_time(void) {
#if  !defined(__APPLE__)
	struct timespec ti;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ti);

	return (uint64_t)ti.tv_sec * MICROSEC + (uint64_t)ti.tv_nsec / (NANOSEC / MICROSEC);
#else
	struct task_thread_times_info aTaskInfo;
	mach_msg_type_number_t aTaskInfoCount = TASK_THREAD_TIMES_INFO_COUNT;
	if (KERN_SUCCESS != task_info(mach_task_self(), TASK_THREAD_TIMES_INFO, (task_info_t )&aTaskInfo, &aTaskInfoCount)) {
		return 0;
	}

	return (uint64_t)(aTaskInfo.user_time.seconds) + (uint64_t)aTaskInfo.user_time.microseconds;
#endif
}

