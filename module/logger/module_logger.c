/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Logger Module
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"
#include "uboss_command.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// 日志结构
struct logger {
	FILE * handle; // 文件句柄
	int close; // 是否关闭
};

// 创建日志
struct logger *
logger_create(void) {
	struct logger * inst = uboss_malloc(sizeof(*inst)); // 分配内存空间
	inst->handle = NULL; // 设置文件句柄为空
	inst->close = 0; // 设置文件是否关闭为0
	return inst; // 返回日志结构的指针
}

// 释放日志
void
logger_release(struct logger * inst) {
	if (inst->close) { // 如果为关闭
		fclose(inst->handle); // 关闭文件句柄
	}
	uboss_free(inst); // 释放日志结构的内存空间
}

// 日志
// 其他服务调用本模块的回调函数
static int
_logger(struct uboss_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct logger * inst = ud;
	fprintf(inst->handle, "[:%08x] ",source); // 日志的源地址
	fwrite(msg, sz , 1, inst->handle); // 写入文件的句柄
	fprintf(inst->handle, "\n"); // 打印文件句柄
	fflush(inst->handle); // 输出到文件

	return 0;
}

// 日志初始化
int
logger_init(struct logger * inst, struct uboss_context *ctx, const char * parm) {
	// 如果 parm 参数存在
	if (parm) {
		inst->handle = fopen(parm,"w"); // 以写入方式打开文件
		if (inst->handle == NULL) { // 如果打开失败
			return 1; // 返回1
		}
		inst->close = 1; // 设置关闭文件句柄
	} else {
		inst->handle = stdout; // 否在直接输出到屏幕
	}

	// 如果文件句柄存在
	if (inst->handle) {
		uboss_callback(ctx, inst, _logger); // 设置回调函数
		uboss_command(ctx, "REG", ".logger"); // 注册本模块
		return 0;
	}
	return 1;
}
