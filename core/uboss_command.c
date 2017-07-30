/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Text Command
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"
#include "uboss_context.h"
#include "uboss_command.h"
#include "uboss_server.h"
#include "uboss_handle.h"
#include "uboss_log.h"
#include "uboss_timer.h"
#include "uboss_monitor.h"
#include "uboss_env.h"
#include "uboss_mq.h"
#include "uboss_module.h"
#include "uboss_lock.h"
#include "uboss_atomic.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>

// ID 转换为 十六进制
static void
id_to_hex(char * str, uint32_t id) {
	int i;
	static char hex[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
	str[0] = ':';
	for (i=0;i<8;i++) {
		str[i+1] = hex[(id >> ((7-i) * 4))&0xf];
	}
	str[9] = '\0';
}

// 退出句柄
static void
handle_exit(struct uboss_context * context, uint32_t handle) {
	if (handle == 0) { // 如果句柄为 "0" 即为自己
		handle = context->handle; // 从上下文获得句柄
		uboss_error(context, "KILL self"); // 杀死自己
	} else {
		uboss_error(context, "KILL :%0x", handle); // 杀死此句柄的服务
	}
	if (G_NODE.monitor_exit) {
		uboss_send(context,  handle, G_NODE.monitor_exit, PTYPE_CLIENT, 0, NULL, 0); // 发送消息
	}
	uboss_handle_retire(handle); // 回收句柄
}

// uboss command
// 命令式
struct command_func {
	const char *name;
	const char * (*func)(struct uboss_context * context, const char * param);
};

// 超时指令
static const char *
cmd_timeout(struct uboss_context * context, const char * param) {
	char * session_ptr = NULL;
	int ti = strtol(param, &session_ptr, 10);
	int session = uboss_context_newsession(context);
	uboss_timeout(context->handle, ti, session);
	sprintf(context->result, "%d", session);
	return context->result;
}

// 注册指令
static const char *
cmd_reg(struct uboss_context * context, const char * param) {
	if (param == NULL || param[0] == '\0') {
		sprintf(context->result, ":%x", context->handle); // 打印句柄到返回结果
		return context->result;
	} else if (param[0] == '.') { // 如果参数第一个字节为 "." 时，执行：
		return uboss_handle_namehandle(context->handle, param + 1); // 关联名字于句柄值
	} else {
		uboss_error(context, "Can't register global name %s in C", param);
		return NULL;
	}
}

// 查询指令
static const char *
cmd_query(struct uboss_context * context, const char * param) {
	if (param[0] == '.') { // 当第一个字节为 "." 时，执行：
		uint32_t handle = uboss_handle_findname(param+1); // 根据名字查找句柄值
		if (handle) {
			sprintf(context->result, ":%x", handle); // 打印句柄到返回结果
			return context->result;
		}
	}
	return NULL;
}

// 名字指令
static const char *
cmd_name(struct uboss_context * context, const char * param) {
	int size = strlen(param); // 计算参数的长度
	char name[size+1];
	char handle[size+1];
	sscanf(param,"%s %s",name,handle); // 扫描参数，生成名字和句柄
	if (handle[0] != ':') { // 如果句柄第一个字节不是冒号时
		return NULL; // 返回空
	}
	uint32_t handle_id = strtoul(handle+1, NULL, 16);
	if (handle_id == 0) {
		return NULL;
	}
	if (name[0] == '.') { // 如果服务名字的第一个字节是 "." 时
		return uboss_handle_namehandle(handle_id, name + 1); // 关联名字于句柄值
	} else {
		uboss_error(context, "Can't set global name %s in C", name);
	}
	return NULL;
}

// 退出指令
static const char *
cmd_exit(struct uboss_context * context, const char * param) {
	handle_exit(context, 0); // 退出回收句柄
	return NULL;
}

// 转换为句柄
static uint32_t
tohandle(struct uboss_context * context, const char * param) {
	uint32_t handle = 0;
	if (param[0] == ':') { // 如果句柄为 ":"
		handle = strtoul(param+1, NULL, 16); // 字符串转换整数
	} else if (param[0] == '.') { // 如果服务名为 "."
		handle = uboss_handle_findname(param+1); // 根据名字查找句柄值
	} else {
		uboss_error(context, "Can't convert %s to handle",param);
	}

	return handle; // 返回句柄
}

// 杀死指令
static const char *
cmd_kill(struct uboss_context * context, const char * param) {
	uint32_t handle = tohandle(context, param); // 转换为句柄
	if (handle) {
		handle_exit(context, handle); // 退出回收句柄
	}
	return NULL;
}

// 开始指令
static const char *
cmd_launch(struct uboss_context * context, const char * param) {
	size_t sz = strlen(param); // 计算参数的长度
	char tmp[sz+1];
	strcpy(tmp,param); // 复制参数到临时字符串
	char * args = tmp;
	char * mod = strsep(&args, " \t\r\n");
	args = strsep(&args, "\r\n");
	struct uboss_context * inst = uboss_context_new(mod,args); // 新建上下文
	if (inst == NULL) {
		return NULL;
	} else {
		id_to_hex(context->result, inst->handle); // ID 转 16进制
		return context->result;
	}
}

// 获得环境指令
static const char *
cmd_getenv(struct uboss_context * context, const char * param) {
	return uboss_getenv(param); // 获得 lua 环境的变量值
}

// 设置环境指令
static const char *
cmd_setenv(struct uboss_context * context, const char * param) {
	size_t sz = strlen(param); // 计算参数的长度
	char key[sz+1];
	int i;
	for (i=0;param[i] != ' ' && param[i];i++) {
		key[i] = param[i];
	}
	if (param[i] == '\0')
		return NULL;

	key[i] = '\0';
	param += i+1;

	uboss_setenv(key,param); // 设置 lua 环境的变量值
	return NULL;
}

// 开始时间指令
static const char *
cmd_starttime(struct uboss_context * context, const char * param) {
	uint32_t sec = uboss_starttime(); // 获得开始时间的秒数
	sprintf(context->result,"%u",sec); // 打印到返回字符串中
	return context->result;
}

// 设置终结服务指令
static const char *
cmd_endless(struct uboss_context * context, const char * param) {
	if (context->endless) { // 如果有终结指令存在
		strcpy(context->result, "1"); // 打印 "1" 到返回字符串中
		context->endless = false;
		return context->result;
	}
	return NULL;
}

// 终止指令
static const char *
cmd_abort(struct uboss_context * context, const char * param) {
	uboss_handle_retireall(); // 回收所有句柄
	return NULL;
}

// 监视器指令
static const char *
cmd_monitor(struct uboss_context * context, const char * param) {
	uint32_t handle=0;
	if (param == NULL || param[0] == '\0') {
		if (G_NODE.monitor_exit) {
			// return current monitor serivce
			sprintf(context->result, ":%x", G_NODE.monitor_exit);
			return context->result;
		}
		return NULL;
	} else {
		handle = tohandle(context, param);
	}
	G_NODE.monitor_exit = handle;
	return NULL;
}

static const char *
cmd_stat(struct uboss_context * context, const char * param) {
	if (strcmp(param, "mqlen") == 0) {
		int len = uboss_mq_length(context->queue);
		sprintf(context->result, "%d", len);
	} else if (strcmp(param, "endless") == 0) {
		if (context->endless) {
			strcpy(context->result, "1");
			context->endless = false;
		} else {
			strcpy(context->result, "0");
		}
	} else if (strcmp(param, "cpu") == 0) {
		double t = (double)context->cpu_cost / 1000000.0;	// microsec
		sprintf(context->result, "%lf", t);
	} else if (strcmp(param, "time") == 0) {
		if (context->profile) {
			uint64_t ti = uboss_thread_time() - context->cpu_start;
			double t = (double)ti / 1000000.0;	// microsec
			sprintf(context->result, "%lf", t);
		} else {
			strcpy(context->result, "0");
		}
	} else if (strcmp(param, "message") == 0) {
		sprintf(context->result, "%d", context->message_count);
	} else {
		context->result[0] = '\0';
	}
	return context->result;
}

// 获得消息队列长度的指令
static const char *
cmd_mqlen(struct uboss_context * context, const char * param) {
	int len = uboss_mq_length(context->queue); // 计算消息队列的长度
	sprintf(context->result, "%d", len); // 转换为字符串
	return context->result; // 返回 长度字符串
}

// 打开日志的指令
static const char *
cmd_logon(struct uboss_context * context, const char * param) {
	uint32_t handle = tohandle(context, param);
	if (handle == 0)
		return NULL;
	struct uboss_context * ctx = uboss_handle_grab(handle);
	if (ctx == NULL)
		return NULL;
	FILE *f = NULL;
	FILE * lastf = ctx->logfile;
	if (lastf == NULL) {
		f = uboss_log_open(context, handle); // 打开日志
		if (f) {
			if (!ATOM_CAS_POINTER(&ctx->logfile, NULL, f)) {
				// logfile opens in other thread, close this one.
				fclose(f);
			}
		}
	}
	uboss_context_release(ctx);
	return NULL;
}

// 关闭日志的指令
static const char *
cmd_logoff(struct uboss_context * context, const char * param) {
	uint32_t handle = tohandle(context, param);
	if (handle == 0)
		return NULL;
	struct uboss_context * ctx = uboss_handle_grab(handle);
	if (ctx == NULL)
		return NULL;
	FILE * f = ctx->logfile;
	if (f) {
		// logfile may close in other thread
		if (ATOM_CAS_POINTER(&ctx->logfile, f, NULL)) {
			uboss_log_close(context, f, handle); // 关闭日志
		}
	}
	uboss_context_release(ctx);
	return NULL;
}

// 信号的指令
static const char *
cmd_signal(struct uboss_context * context, const char * param) {
	uint32_t handle = tohandle(context, param);
	if (handle == 0)
		return NULL;
	struct uboss_context * ctx = uboss_handle_grab(handle);
	if (ctx == NULL)
		return NULL;
	param = strchr(param, ' ');
	int sig = 0;
	if (param) {
		sig = strtol(param, NULL, 0);
	}
	// NOTICE: the signal function should be thread safe.
	uboss_module_instance_signal(ctx->mod, ctx->instance, sig);

	uboss_context_release(ctx);
	return NULL;
}

// 指令列表
static struct command_func cmd_funcs[] = {
	{ "TIMEOUT", cmd_timeout },
	{ "REG", cmd_reg },
	{ "QUERY", cmd_query },
	{ "NAME", cmd_name },
	{ "EXIT", cmd_exit },
	{ "KILL", cmd_kill },
	{ "LAUNCH", cmd_launch },
	{ "GETENV", cmd_getenv },
	{ "SETENV", cmd_setenv },
	{ "STARTTIME", cmd_starttime },
	{ "ENDLESS", cmd_endless },
	{ "ABORT", cmd_abort },
	{ "MONITOR", cmd_monitor },
	{ "STAT", cmd_stat },
	{ "MQLEN", cmd_mqlen },
	{ "LOGON", cmd_logon },
	{ "LOGOFF", cmd_logoff },
	{ "SIGNAL", cmd_signal },
	{ NULL, NULL },
};

// 调用命令
const char *
uboss_command(struct uboss_context * context, const char * cmd , const char * param) {
	struct command_func * method = &cmd_funcs[0]; // 取出命令函数的结构第一个指针
	while(method->name) { // 取出方法的名字，并循环
		if (strcmp(cmd, method->name) == 0) { // 比较
			return method->func(context, param); // 调用回调函数
		}
		++method; // 取下一个指针
	}

	return NULL;
}
