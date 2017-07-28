/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Module Loader
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/


#include "uboss.h"

#include "uboss_module.h"
#include "uboss_lock.h"

#include <assert.h>
#include <string.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

// 模块的最大数量（即类型数量）
#define MAX_MODULE_TYPE 32

// 模块的结构
struct modules {
	int count; // 数量
	struct spinlock lock; // 锁
	const char * path; // 模块的路径
	struct uboss_module m[MAX_MODULE_TYPE]; // 模块的数组
};

static struct modules * M = NULL; // 声明模块的结构

// 尝试打开动态库
static void *
_try_open(struct modules *m, const char * name) {
	const char *l;
	const char * path = m->path; // 获得模块的路径
	size_t path_size = strlen(path); // 计算路径的长度
	size_t name_size = strlen(name); // 计算模块名的长度

	int sz = path_size + name_size; // 计算模块的路径和名字长度
	//search path
	void * dl = NULL;
	char tmp[sz]; // 临时字符串

	// 循环每个路径，尝试打开 name 的模块
	do
	{
		memset(tmp,0,sz); // 字符串清零
		while (*path == ';') path++; // 遇到 ';' 则循环过滤掉所有分号
		if (*path == '\0') break; // 如果遇到 \0 则结束循环
		l = strchr(path, ';'); // 查找 分号 的位置
		if (l == NULL) l = path + strlen(path);
		int len = l - path; // 计算下一个 ';' 到现在 path 的字符数
		int i;

		// 循环把不等于 '?' 的字符复制到临时数组
		for (i=0;path[i]!='?' && i < len ;i++) {
			tmp[i] = path[i];
		}
		memcpy(tmp+i,name,name_size); // 复制 文件名到路径后面
		if (path[i] == '?') {
			strncpy(tmp+i+name_size,path+i+1,len - i - 1); // 加上 '?' 后面和 ';' 前面的字符
		} else {
			fprintf(stderr,"Invalid Module path\n");
			exit(1);
		}
		dl = dlopen(tmp, RTLD_NOW | RTLD_GLOBAL); // 尝试打开 动态链接库
		path = l; // 把新地址赋给 path
	}while(dl == NULL);

	// 如果指针为空，表示打开所有路径下的 name 模块失败
	if (dl == NULL) {
		fprintf(stderr, "try open %s failed : %s\n",name,dlerror());
	}

	return dl;
}

// 根据名字查询
static struct uboss_module *
_query(const char * name) {
	int i;
	for (i=0;i<M->count;i++) { // 循环所有模块
		if (strcmp(M->m[i].name,name)==0) { // 比较名字是否和模块数组匹配
			return &M->m[i]; // 返回找到的模块指针
		}
	}
	return NULL;
}

// 打开模块中函数
static int
_open_sym(struct uboss_module *mod) {
	size_t name_size = strlen(mod->name); // 获得模块的名字
	char tmp[name_size + 9]; // create/init/release/signal , longest name is release (7)
	memcpy(tmp, mod->name, name_size); // 将名字复制到临时字符串
	strcpy(tmp+name_size, "_create"); // 将创建名加到临时字符串中
	mod->create = dlsym(mod->module, tmp); // 实例化模块中的创建函数，并赋给模块的结构
	strcpy(tmp+name_size, "_init"); // 将初始化名加到临时字符串中
	mod->init = dlsym(mod->module, tmp); // 实例化模块中的初始化函数，并赋给模块的结构
	strcpy(tmp+name_size, "_release"); // 将释放名加到临时字符串中
	mod->release = dlsym(mod->module, tmp); // 实例化模块中的释放函数，并赋给模块的结构
	strcpy(tmp+name_size, "_signal"); // 将信号名加到临时字符串中
	mod->signal = dlsym(mod->module, tmp); // 实例化模块中的信号函数，并赋给模块的结构

	return mod->init == NULL;
}

// 查询模块
struct uboss_module *
uboss_module_query(const char * name) {
	struct uboss_module * result = _query(name); // 根据名字查询模块数组，返回模块结构的指针
	if (result) // 如果找到直接返回即可
		return result;

	// 如果结构为空，则锁住模块数组，准备尝试再打开模块。
	SPIN_LOCK(M) // 锁住

	// 锁组数组后，再次尝试查找模块是否存在，不存在则尝试打开。
	result = _query(name); // double check

	// 如果结果为空，且总数小于最大模块数
	if (result == NULL && M->count < MAX_MODULE_TYPE) {
		int index = M->count; // 获得模块总数
		void * dl = _try_open(M,name); // 尝试打开模块
		if (dl) {
			M->m[index].name = name; // 模块名
			M->m[index].module = dl; // 模块指针，给 dlsym() 函数使用

			// 打开模块的函数
			if (_open_sym(&M->m[index]) == 0) {
				M->m[index].name = uboss_strdup(name); // 复制一份字符串
				M->count ++; // 模块数加一
				result = &M->m[index]; // 获得模块的结构
			}
		}
	}

	SPIN_UNLOCK(M) // 解开锁

	return result; // 返回模块的结构
}

// 插入模块结构到模块数组
void
uboss_module_insert(struct uboss_module *mod) {
	SPIN_LOCK(M) // 锁住

	struct uboss_module * m = _query(mod->name); // 根据名字查询模块数组，返回模块结构的指针
	assert(m == NULL && M->count < MAX_MODULE_TYPE);
	int index = M->count; // 获得模块的数量
	M->m[index] = *mod; // 将模块结构赋给模块数组
	++M->count; // 模块数加一

	SPIN_UNLOCK(M) // 解锁
}

// 从模块中获取实例化的创建函数指针
void *
uboss_module_instance_create(struct uboss_module *m) {
	if (m->create) { // 如果创建函数存在
		return m->create(); // 返回调用模块中的创建函数
	} else {
		// C语言中 ~ 符号为按位取反的意思
		// 例如： ~0x37=~(0011 0111)=(1100 1000)=0xC8
		return (void *)(intptr_t)(~0); // 返回空指针 即：0xFFFF FFFF
	}
}

// 从模块中获取实例化的初始化函数指针
// 模块中这个初始化函数是必须的
int
uboss_module_instance_init(struct uboss_module *m, void * inst, struct uboss_context *ctx, const char * parm) {
	return m->init(inst, ctx, parm); // 返回调用模块中的初始化函数
}

// 从模块中获取实例化的释放函数指针
void
uboss_module_instance_release(struct uboss_module *m, void *inst) {
	if (m->release) { // 如果释放函数存在
		m->release(inst); // 返回调用模块中的释放函数
	}
}

// 从模块中获取实例化的信号函数指针
void
uboss_module_instance_signal(struct uboss_module *m, void *inst, int signal) {
	if (m->signal) { // 如果信号函数存在
		m->signal(inst, signal); // 返回调用模块中的信号函数
	}
}

// 初始化模块
void
uboss_module_init(const char *path) {
	struct modules *m = uboss_malloc(sizeof(*m)); // 分配模块结构指针的内存空
	m->count = 0; // 将模块数设置为0
	m->path = uboss_strdup(path); // 设置模块的路径

	SPIN_INIT(m) // 初始化锁

	M = m; // 将模块结构指针赋值给全局变量
}

