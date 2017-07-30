/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Thread Monitor
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"

#include "uboss_monitor.h"
#include "uboss_context.h"
#include "uboss_log.h"
#include "uboss_atomic.h"

#include <stdlib.h>
#include <string.h>

// 监视器的结构
struct uboss_monitor {
	int version; // 版本
	int check_version; // 检查版本
	uint32_t source; // 来源地址
	uint32_t destination; // 目的地址
};

// 新建监视器
struct uboss_monitor *
uboss_monitor_new() {
	struct uboss_monitor * ret = uboss_malloc(sizeof(*ret)); // 分配内存空间
	memset(ret, 0, sizeof(*ret)); // 清空内存空间
	return ret; // 返回指针
}

// 删除监视器
void
uboss_monitor_delete(struct uboss_monitor *um) {
	uboss_free(um); // 释放监视器的结构
}

// 触发监视器
void
uboss_monitor_trigger(struct uboss_monitor *um, uint32_t source, uint32_t destination) {
	um->source = source; // 来源地址
	um->destination = destination; // 目的地址
	ATOM_INC(&um->version); // 版本号加一
}

// 检查监视器
void
uboss_monitor_check(struct uboss_monitor *um) {
	if (um->version == um->check_version) { // 比较版本和检查版本的值
		if (um->destination) { // 目的地址
			uboss_context_endless(um->destination); // 结束目标服务
			uboss_error(NULL, "A message from [ :%08x ] to [ :%08x ] maybe in an endless loop (version = %d)", um->source , um->destination, um->version);
		}
	} else {
		// 因为监视线程大约每5秒钟检查一次，调用时设置两个版本相等。
		// 等待5秒钟后，如果当前版本没有增加，则表示该服务还在运行，
		// 有可能服务已经进入死循环，则打印消息，以便上层知晓后处理。
		um->check_version = um->version; // 设置版本和检查版本的值
	}
}
