/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Server
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

// 获得当前上下文的句柄值
uint32_t
uboss_current_handle(void) {
	if (G_NODE.init) {
		void * handle = pthread_getspecific(G_NODE.handle_key);
		return (uint32_t)(uintptr_t)handle;
	} else {
		uint32_t v = (uint32_t)(-THREAD_MAIN);
		return v;
	}
}

// 是否为远程
int
uboss_isremote(struct uboss_context * ctx, uint32_t handle, int * harbor) {
	int ret = 0;
// TODO: 屏蔽远程消息检测
//	int ret = uboss_harbor_message_isremote(handle);
	if (harbor) {
		*harbor = (int)(handle >> HANDLE_REMOTE_SHIFT);
	}
	return ret;
}




//TODO: 屏蔽和远程消息有关代码
/*
static void
copy_name(char name[GLOBALNAME_LENGTH], const char * addr) {
	int i;
	for (i=0;i<GLOBALNAME_LENGTH && addr[i];i++) {
		name[i] = addr[i];
	}
	for (;i<GLOBALNAME_LENGTH;i++) {
		name[i] = '\0';
	}
}
*/

// 根据名字请求
uint32_t
uboss_queryname(struct uboss_context * context, const char * name) {
	switch(name[0]) { // 取出第一个字符
	case ':': // 冒号，为数字型名称，需要操作后返回
		return strtoul(name+1,NULL,16);
	case '.': // 点，为字符串名称，直接返回
		return uboss_handle_findname(name + 1);
	}
	uboss_error(context, "Don't support query global name %s",name);
	return 0;
}

// 过滤参数
static void
_filter_args(struct uboss_context * context, int type, int *session, void ** data, size_t * sz) {
	int needcopy = !(type & PTYPE_TAG_DONTCOPY);
	int allocsession = type & PTYPE_TAG_ALLOCSESSION;
	type &= 0xff;

	// 允许会话
	if (allocsession) {
		assert(*session == 0);
		*session = uboss_context_newsession(context); // 新建会话
	}

	//  需要复制数据
	if (needcopy && *data) {
		char * msg = uboss_malloc(*sz+1); // 分配数据的内存空间
		memcpy(msg, *data, *sz); // 复制数据到内存空间
		msg[*sz] = '\0'; // 设置新数据的最后为0
		*data = msg; // 将新数据的地址复制给原来数据地址
	}

	// 重建数据长度的值
	*sz |= (size_t)type << MESSAGE_TYPE_SHIFT;
}

// 发送消息
int
uboss_send(struct uboss_context * context, uint32_t source, uint32_t destination , int type, int session, void * data, size_t sz) {
	if ((sz & MESSAGE_TYPE_MASK) != sz) {
		uboss_error(context, "The message to %x is too large", destination);
		if (type & PTYPE_TAG_DONTCOPY) {
			uboss_free(data); // 释放数据的内存空间
		}
		return -1;
	}
	_filter_args(context, type, &session, (void **)&data, &sz); // 过滤参数

	// 如果来源地址为0，表示服务自己
	if (source == 0) {
		source = context->handle;
	}

	// 如果目的地址为0，返回会话ID
	if (destination == 0) {
		return session;
	}
//TODO:屏蔽和远程消息有关代码
//	if (uboss_harbor_message_isremote(destination)) {
//		struct remote_message * rmsg = uboss_malloc(sizeof(*rmsg)); // 分配远程消息的内存空间
//		rmsg->destination.handle = destination; // 目的地址
//		rmsg->message = data; // 数据的地址
//		rmsg->sz = sz; // 数据的长度
//		uboss_harbor_send(rmsg, source, session); // 发送消息到集群中
//	} else { // 本地消息
		struct uboss_message smsg;
		smsg.source = source;
		smsg.session = session;
		smsg.data = data;
		smsg.sz = sz;

		// 将消息压入消息队列
		if (uboss_context_push(destination, &smsg)) {
			uboss_free(data);
			return -1;
		}
//	}
	return session;
}

// 以服务名字来发送消息
int
uboss_sendname(struct uboss_context * context, uint32_t source, const char * addr , int type, int session, void * data, size_t sz) {
	if (source == 0) {
		source = context->handle;
	}
	uint32_t des = 0;
	if (addr[0] == ':') {
		des = strtoul(addr+1, NULL, 16);
	} else if (addr[0] == '.') {
		des = uboss_handle_findname(addr + 1);
		if (des == 0) {
			if (type & PTYPE_TAG_DONTCOPY) {
				uboss_free(data);
			}
			return -1;
		}
//TODO:屏蔽和远程消息有管代码
//	} else {
//		_filter_args(context, type, &session, (void **)&data, &sz);

//		struct remote_message * rmsg = uboss_malloc(sizeof(*rmsg));
//		copy_name(rmsg->destination.name, addr);
//		rmsg->destination.handle = 0;
//		rmsg->message = data;
//		rmsg->sz = sz;

//		uboss_harbor_send(rmsg, source, session);
//		return session;
	}

	return uboss_send(context, source, des, type, session, data, sz);
}



// 注册服务模块中的返回函数
void
uboss_callback(struct uboss_context * context, void *ud, uboss_cb cb) {
	context->cb = cb; // 返回函数的指针
	context->cb_ud = ud; // 用户的数据指针
}



// 全局线程管道初始化
void
uboss_globalinit(void) {
	G_NODE.total = 0;
	G_NODE.monitor_exit = 0;
	G_NODE.init = 1;
	if (pthread_key_create(&G_NODE.handle_key, NULL)) {
		fprintf(stderr, "pthread_key_create failed");
		exit(1);
	}
	// set mainthread's key
	uboss_initthread(THREAD_MAIN); // 初始化主线程
}

// 退出全局线程管道
void
uboss_globalexit(void) {
	pthread_key_delete(G_NODE.handle_key);
}

// 初始化线程管道
void
uboss_initthread(int m) {
	uintptr_t v = (uint32_t)(-m);
	pthread_setspecific(G_NODE.handle_key, (void *)v);
}

