/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Start Function
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#ifndef UBOSS_START_H
#define UBOSS_START_H

// 配置文件的结构
struct uboss_config {
	const char * root; // 根目录
	int thread; // 线程数
	int harbor; // 集群Id
	const char * daemon; // 守护
	const char * module_path; // 模块的路径
	const char * bootstrap; // 引导程序
	const char * logservice; // 日志记录器的服务
	const char * logger; // 日志记录器
	const char * logpath; // 保存日志的路径
};

#define THREAD_WORKER 0
#define THREAD_MAIN 1
#define THREAD_SOCKET 2
#define THREAD_TIMER 3
#define THREAD_MONITOR 4

// 启动 Skynet
void uboss_start(struct uboss_config * config);


#endif /* UBOSS_START_H */
