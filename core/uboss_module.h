/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Module Loader
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#ifndef UBOSS_MODULE_H
#define UBOSS_MODULE_H

struct uboss_context;

// 回调函数
typedef void * (*uboss_dl_create)(void);
typedef int (*uboss_dl_init)(void * inst, struct uboss_context *, const char * parm);
typedef void (*uboss_dl_release)(void * inst);
typedef void (*uboss_dl_signal)(void * inst, int signal);

struct uboss_module {
	const char * name; // 模块名称
	void * module; // 模块的地址
	uboss_dl_create create; // 创建的回调函数地址
	uboss_dl_init init; // 初始化的回调函数地址
	uboss_dl_release release; // 释放的回调函数地址
	uboss_dl_signal signal; // 信号的回调函数地址
};

void uboss_module_insert(struct uboss_module *mod);
struct uboss_module * uboss_module_query(const char * name);
void * uboss_module_instance_create(struct uboss_module *m);
int uboss_module_instance_init(struct uboss_module *m, void * inst, struct uboss_context *ctx, const char * parm);
void uboss_module_instance_release(struct uboss_module *m, void *inst);
void uboss_module_instance_signal(struct uboss_module *m, void *inst, int signal);

void uboss_module_init(const char *path);

#endif /* UBOSS_MODULE_H */
