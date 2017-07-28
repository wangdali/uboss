/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Start Function
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"
#include "uboss_server.h"
#include "uboss_start.h"
#include "uboss_mq.h"
#include "uboss_handle.h"
#include "uboss_module.h"
#include "uboss_timer.h"
#include "uboss_monitor.h"

#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 监视器的结构
struct monitor {
	int count; // 总数
	struct uboss_monitor ** m; // uBoss监视器的结构
	pthread_cond_t cond; // 线程条件变量
	pthread_mutex_t mutex; // 线程互斥锁
	int sleep; // 休眠标志
	int quit; // 退出标志
};

// 工作线程的参数
struct worker_parm {
	struct monitor *m; // 监视器的结构
	int id; // 编号
	int weight; // 权重
};

#define CHECK_ABORT if (uboss_context_total()==0) break;

// 封装 创建线程
static void
create_thread(pthread_t *thread, void *(*start_routine) (void *), void *arg) {
	if (pthread_create(thread,NULL, start_routine, arg)) {
		fprintf(stderr, "Create thread failed");
		exit(1); // 退出
	}
}

// 唤醒
static void
wakeup(struct monitor *m, int busy) {
	if (m->sleep >= m->count - busy) {
		// signal sleep worker, "spurious wakeup" is harmless
		pthread_cond_signal(&m->cond); // 激发等待 &m->cond 条件的线程
	}
}


// 释放 监视器
static void
free_monitor(struct monitor *m) {
	int i;
	int n = m->count;
	for (i=0;i<n;i++) {
		uboss_monitor_delete(m->m[i]); // 删除监视器
	}
	pthread_mutex_destroy(&m->mutex);
	pthread_cond_destroy(&m->cond);
	uboss_free(m->m); // 释放内存空间
	uboss_free(m); // 释放内存空间
}

// 监视器 线程
static void *
thread_monitor(void *p) {
	struct monitor * m = p;
	int i;
	int n = m->count;
	uboss_initthread(THREAD_MONITOR); // 初始化线程
	for (;;) {
		CHECK_ABORT
		for (i=0;i<n;i++) {
			uboss_monitor_check(m->m[i]);  // 监视器 检查版本
		}

		// 循环 5 次，每次睡眠 1秒
		for (i=0;i<5;i++) {
			CHECK_ABORT
			sleep(1); // 睡眠1秒钟
		}
	}

	return NULL;
}

// 定时器 线程
static void *
thread_timer(void *p) {
	struct monitor * m = p;
	uboss_initthread(THREAD_TIMER); // 初始化线程
	for (;;) {
		uboss_updatetime(); // 更新事件
		CHECK_ABORT
		wakeup(m,m->count-1); // 唤醒
		usleep(2500); // 睡眠 1/4 秒
	}
	// wakeup socket thread
//TODO:屏蔽和socket有关代码
//	uboss_socket_exit();  // 退出 Socket
	// wakeup all worker thread
	pthread_mutex_lock(&m->mutex); // 设置互斥锁
	m->quit = 1; // 设置退出标志
	pthread_cond_broadcast(&m->cond); // 激发所有线程
	pthread_mutex_unlock(&m->mutex); // 解开互斥锁
	return NULL;
}

// 工作线程
static void *
thread_worker(void *p) {
	struct worker_parm *wp = p;
	int id = wp->id;
	int weight = wp->weight;
	struct monitor *m = wp->m;
	struct uboss_monitor *sm = m->m[id];
	uboss_initthread(THREAD_WORKER); // 初始化线程
	struct message_queue * q = NULL;
	while (!m->quit) {
		q = uboss_context_message_dispatch(sm, q, weight); // 核心功能: 从消息队列中取出服务的消息
		if (q == NULL) {
			if (pthread_mutex_lock(&m->mutex) == 0) { // 设置互斥锁
				++ m->sleep;
				// "spurious wakeup" is harmless,
				// because uboss_context_message_dispatch() can be call at any time.
				if (!m->quit)
					pthread_cond_wait(&m->cond, &m->mutex); // 无条件等待
				-- m->sleep;
				if (pthread_mutex_unlock(&m->mutex)) { // 解开互斥锁
					fprintf(stderr, "unlock mutex error");
					exit(1);
				}
			}
		}
	}
	return NULL;
}

// 启动线程
static void
start(int thread) {
	pthread_t pid[thread+2]; // 启动线程数量

	struct monitor *m = uboss_malloc(sizeof(*m)); // 分配指针的内存空间
	memset(m, 0, sizeof(*m)); // 内存清空
	m->count = thread; // 线程数量
	m->sleep = 0; // 休眠标志

	m->m = uboss_malloc(thread * sizeof(struct uboss_monitor *)); // 分配监视器结构的内存空间
	int i;

	// 循环所有线程
	for (i=0;i<thread;i++) {
		m->m[i] = uboss_monitor_new(); // 新建 监视器
	}

	// 初始化线程互斥锁
	if (pthread_mutex_init(&m->mutex, NULL)) {
		fprintf(stderr, "Init mutex error");
		exit(1);
	}

	// 初始化线程条件变量
	if (pthread_cond_init(&m->cond, NULL)) {
		fprintf(stderr, "Init cond error");
		exit(1);
	}

	create_thread(&pid[0], thread_monitor, m); // 创建 监视器 线程
	create_thread(&pid[1], thread_timer, m); // 创建 定时器 线程

	// 权重
	static int weight[] = {
		-1, -1, -1, -1, 0, 0, 0, 0,
		1, 1, 1, 1, 1, 1, 1, 1,
		2, 2, 2, 2, 2, 2, 2, 2,
		3, 3, 3, 3, 3, 3, 3, 3, };

	// 循环启动 工作线程
	struct worker_parm wp[thread];
	for (i=0;i<thread;i++) {
		wp[i].m = m;
		wp[i].id = i;
		if (i < sizeof(weight)/sizeof(weight[0])) {
			wp[i].weight= weight[i];
		} else {
			wp[i].weight = 0;
		}
		create_thread(&pid[i+2], thread_worker, &wp[i]); // 创建 工作 线程
	}

	// 等待 线程 结束
	for (i=0;i<thread+2;i++) {
		pthread_join(pid[i], NULL);
	}

	free_monitor(m); // 释放监视器
}

// 引导程序
static void
bootstrap(struct uboss_context * logger, const char * cmdline) {
	int sz = strlen(cmdline);
	char name[sz+1];
	char args[sz+1];
	sscanf(cmdline, "%s %s", name, args); // 将 cmdline 分解为 name 和 args
	struct uboss_context *ctx = uboss_context_new(name, args); // 新建 uBoss 上下文
	if (ctx == NULL) {
		uboss_error(NULL, "Bootstrap error : %s\n", cmdline);
		uboss_context_dispatchall(logger);
		exit(1);
	}
}

// 启动 uBoss 框架
void
uboss_start(struct uboss_config * config) {
	uboss_handle_init(config->harbor); // 初始化 句柄模块
	uboss_mq_init(); // 初始化 消息队列模块
	uboss_module_init(config->module_path); // 初始化 加载模块
	uboss_timer_init(); // 初始化 定时器模块

	// 创建新的 uBoss 上下文， 用于 日志记录器
	struct uboss_context *ctx = uboss_context_new(config->logservice, config->logger);
	if (ctx == NULL) {
		fprintf(stderr, "Can't launch %s service\n", config->logservice);
		exit(1);
	}

	// 加载 引导程序 的lua脚本
	bootstrap(ctx, config->bootstrap);

	// 启动线程
	start(config->thread);

}
