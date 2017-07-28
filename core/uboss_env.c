/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Lua Env
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"
#include "uboss_env.h"
#include "uboss_lock.h"

#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <assert.h>

// 环境结构
struct uboss_env {
	struct spinlock lock; // 锁
	lua_State *L; // Lua VM
};

// 声明环境结构的全局变量
static struct uboss_env *E = NULL;

// 获取环境变量
const char *
uboss_getenv(const char *key) {
	SPIN_LOCK(E) // 锁住

	lua_State *L = E->L; // 获得 Lua VM

	lua_getglobal(L, key); // 从Lua VM中获得key的环境变量
	const char * result = lua_tostring(L, -1); // 将值转换为C的字符串
	lua_pop(L, 1); // 弹出

	SPIN_UNLOCK(E) // 解锁

	return result;
}

// 设置环境变量
void
uboss_setenv(const char *key, const char *value) {
	SPIN_LOCK(E) // 锁住

	lua_State *L = E->L; // 获得 Lua VM
	lua_getglobal(L, key); // 从Lua VM中获得key的环境变量
	assert(lua_isnil(L, -1));
	lua_pop(L,1); // 弹出
	lua_pushstring(L,value); // 压入Key的值
	lua_setglobal(L,key); // 设置环境变量

	SPIN_UNLOCK(E) // 解锁
}

// 初始化环境变量
void
uboss_env_init() {
	E = uboss_malloc(sizeof(*E)); // 分配内存空间
	SPIN_INIT(E) // 初始化锁
	E->L = luaL_newstate(); // 创建新的 Lua VM
}
