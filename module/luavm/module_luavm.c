/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss LuaVM Module
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"
#include "uboss_command.h"
#include "uboss_log.h"
#include "uboss_server.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// uBoss 的 luaVM 沙盒
struct luavm {
	lua_State * L; // luaVM
	struct uboss_context * ctx; // uBoss 上下文
};

// LUA_CACHELIB may defined in patched lua for shared proto
#ifdef LUA_CACHELIB

#define codecache luaopen_cache

#else
// 下面是不使用修改的Lua版本时，提供的替代功能

// 虚假的清理函数
static int
cleardummy(lua_State *L) {
  return 0;
}

// 定义 Lua 的 codecache 库
// 实际功能在修改的Lua代码中
static int
codecache(lua_State *L) {
	luaL_Reg l[] = {
		{ "clear", cleardummy }, // 清理
		{ "mode", cleardummy }, // 模式
		{ NULL, NULL },
	};
	luaL_newlib(L,l); // 新库
	lua_getglobal(L, "loadfile");
	lua_setfield(L, -2, "loadfile");
	return 1;
}

#endif

// 追踪
static int
traceback (lua_State *L) {
	const char *msg = lua_tostring(L, 1); // 获得消息
	if (msg)
		luaL_traceback(L, L, msg, 1); // 追踪消息
	else {
		lua_pushliteral(L, "(no error message)"); // 压入文字 没有错误消息
	}
	return 1;
}

// 报告启动错误
static void
_report_launcher_error(struct uboss_context *ctx) {
	// sizeof "ERROR" == 5
	uboss_sendname(ctx, 0, ".launcher", PTYPE_TEXT, 0, "ERROR", 5);
}

// 字符串
static const char *
optstring(struct uboss_context *ctx, const char *key, const char * str) {
	const char * ret = uboss_command(ctx, "GETENV", key); // 获得环境字符串
	if (ret == NULL) {
		return str;
	}
	return ret;
}

// 初始化
static int
_init(struct luavm *l, struct uboss_context *ctx, const char * args, size_t sz) {
	lua_State *L = l->L; // 设置 luaVM 状态机的地址
	l->ctx = ctx; // 设置 uBoss 上下文
	lua_gc(L, LUA_GCSTOP, 0); // 设置 GC 停止

	lua_pushboolean(L, 1); // 压入布尔值1 /* signal for libraries to ignore env. vars. */
	lua_setfield(L, LUA_REGISTRYINDEX, "LUA_NOENV"); // 设置表 LUA_REGISTRYINDEX 中 LUA_NOENV 字段

	luaL_openlibs(L); // 打开 lua 标准库

	lua_pushlightuserdata(L, ctx); // 压入用户数据
	lua_setfield(L, LUA_REGISTRYINDEX, "uboss_context"); // 设置表 LUA_REGISTRYINDEX 中 uboss_context 字段

	// 注册 Lua 的 codecache 库，名为：uboss.codecache
	luaL_requiref(L, "uboss.codecache", codecache , 0);
	lua_pop(L,1); // 弹出堆栈

	// 设置全局变量
	const char *path = optstring(ctx, "lua_path","./framework/?.lua;./framework/?/init.lua"); // lua脚本的路径
	lua_pushstring(L, path); // 压入框架脚本的路径字符串
	lua_setglobal(L, "LUA_PATH"); // 设置为全局变量
	const char *cpath = optstring(ctx, "lua_lib","./lib/?.so"); // lua库的路径
	lua_pushstring(L, cpath); // 压入 lua 库的路径字符串
	lua_setglobal(L, "LUA_LIB"); // 设置为全局变量
	const char *service = optstring(ctx, "lua_service", "./service/?.lua"); // lua服务的路径
	lua_pushstring(L, service); // 压入 lua 服务脚本的路径字符串
	lua_setglobal(L, "LUA_SERVICE"); // 设置为全局变量
	const char *preload = uboss_command(ctx, "GETENV", "preload"); // 预加载
	lua_pushstring(L, preload); // 压入 预加载脚本的路径字符串
	lua_setglobal(L, "LUA_PRELOAD"); // 设置为全局变量

	lua_pushcfunction(L, traceback); // 追踪
	assert(lua_gettop(L) == 1); // 断言获得堆栈顶 == 1

	const char * loader = optstring(ctx, "lua_loader", "./framework/loader.lua"); // lua的加载器脚本名字

	// 执行加载器脚本
	int r = luaL_loadfile(L,loader); // 执行所有 lua 脚本，都必须要用 loader 加载器

	// 加载脚本失败
	if (r != LUA_OK) {
		uboss_error(ctx, "Can't load %s : %s", loader, lua_tostring(L, -1));
		_report_launcher_error(ctx);
		return 1;
	}

	// 压入参数
	lua_pushlstring(L, args, sz); // 压入执行的 lua 脚本路径

	// 执行函数
	r = lua_pcall(L,1,0,1); // 执行 loader 加载器

	// 执行脚本失败
	if (r != LUA_OK) {
		uboss_error(ctx, "lua loader error : %s", lua_tostring(L, -1));
		_report_launcher_error(ctx);
		return 1;
	}

	lua_settop(L,0); // 设置 堆栈的顶为0

	lua_gc(L, LUA_GCRESTART, 0); // 重启 GC

	return 0;
}

// 启动
static int
_launch(struct uboss_context * context, void *ud, int type, int session, uint32_t source , const void * msg, size_t sz) {
	assert(type == 0 && session == 0); // 断言
	struct luavm *l = ud; // 用户数据指针
	uboss_callback(context, NULL, NULL); // 清空指定context的回调函数设置
	int err = _init(l, context, msg, sz); // 初始化
	if (err) {
		uboss_command(context, "EXIT", NULL); // 退出命令
	}

	return 0;
}

// 初始化
int
luavm_init(struct luavm *l, struct uboss_context *ctx, const char * args) {
	int sz = strlen(args); // 计算参数的长度
	char * tmp = uboss_malloc(sz); // 分配内存空间
	memcpy(tmp, args, sz); // 复制参数到内存空间
	uboss_callback(ctx, l , _launch); // 设置回调函数
	const char * self = uboss_command(ctx, "REG", NULL); // 执行注册命令
	uint32_t handle_id = strtoul(self+1, NULL, 16); // 获得 句柄值

	// 它必须时第一个消息
	// it must be first message
	uboss_send(ctx, 0, handle_id, PTYPE_TAG_DONTCOPY,0, tmp, sz); // 发送消息到框架
	return 0;
}

// 创建
struct luavm *
luavm_create(void) {
	struct luavm * l = uboss_malloc(sizeof(*l)); // 分配内存空间
	memset(l,0,sizeof(*l)); // 清空内存
	l->L = lua_newstate(uboss_lalloc, NULL); // 新建 luaVM 状态机
	return l;
}

// 释放
void
luavm_release(struct luavm *l) {
	lua_close(l->L); // 关闭 luaVM
	uboss_free(l); // 释放内存空间
}

// 信号
void
luavm_signal(struct luavm *l, int signal) {
	uboss_error(l->ctx, "recv a signal %d", signal); // 打印接收到一个信号的消息
#ifdef lua_checksig
	// 修改位置为：lua.h 和 lvm.c
	// If our lua support signal (modified lua version by uboss), trigger it.
	uboss_sig_L = l->L;
#endif
}
