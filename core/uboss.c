/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Main Function
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"
#include "uboss_env.h"
#include "uboss_server.h"
#include "uboss_start.h"
#include "uboss_handle.h"
#include "uboss_timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <signal.h>
#include <assert.h>

// 复制字符串
char *
uboss_strdup(const char *str) {
	size_t sz = strlen(str);
	char * ret = uboss_malloc(sz+1);
	memcpy(ret, str, sz+1);
	return ret;
}


// 设置 整数类型 的 K-V 到 lua 环境中
static int
optint(const char *key, int opt) {
	const char * str = uboss_getenv(key);
	if (str == NULL) {
		char tmp[20];
		sprintf(tmp,"%d",opt);
		uboss_setenv(key, tmp);
		return opt;
	}
	return strtol(str, NULL, 10);
}

/*
// 设置 布尔类型 的 K-V 到 lua 环境中
static int
optboolean(const char *key, int opt) {
	const char * str = uboss_getenv(key);
	if (str == NULL) {
		uboss_setenv(key, opt ? "true" : "false");
		return opt;
	}
	return strcmp(str,"true")==0;
}
*/

// 设置 字符串类型 的 K-V 到 lua 环境中
static const char *
optstring(const char *key,const char * opt) {
	const char * str = uboss_getenv(key);
	if (str == NULL) {
		if (opt) {
			uboss_setenv(key, opt);
			opt = uboss_getenv(key);
		}
		return opt;
	}
	return str;
}

// 初始化环境
static void
_init_env(lua_State *L) {
	lua_pushnil(L);  /* first key */
	while (lua_next(L, -2) != 0) {
		int keyt = lua_type(L, -2);
		if (keyt != LUA_TSTRING) {
			fprintf(stderr, "Invalid config table\n");
			exit(1);
		}
		const char * key = lua_tostring(L,-2);
		if (lua_type(L,-1) == LUA_TBOOLEAN) {
			int b = lua_toboolean(L,-1);
			uboss_setenv(key,b ? "true" : "false" );
		} else {
			const char * value = lua_tostring(L,-1);
			if (value == NULL) {
				fprintf(stderr, "Invalid config table key = %s\n", key);
				exit(1);
			}
			uboss_setenv(key,value);
		}
		lua_pop(L,1);
	}
	lua_pop(L,1);
}

// 信号处理，屏蔽 SIGPIPE 信号，避免进程退出
int sigign() {
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, 0);
	return 0;
}

// 加载 lua 脚本
static const char * load_config = "\
	local config_name = ...\
	local f = assert(io.open(config_name))\
	local code = assert(f:read \'*a\')\
	local function getenv(name) return assert(os.getenv(name), \'os.getenv() failed: \' .. name) end\
	code = string.gsub(code, \'%$([%w_%d]+)\', getenv)\
	f:close()\
	local result = {}\
	assert(load(code,\'=(load)\',\'t\',result))()\
	return result\
";

void
logo() {
	int ret = system("reset"); // 清空终端的屏幕
	if(ret==1)ret=0;
//	fprintf(stdout,"\033[2J");
	fprintf(stdout, "================================================================================\n");
	fprintf(stdout, "\n\n");
	fprintf(stdout, "                        ********              Powered by uboss.org     \n");
	fprintf(stdout, "                          **    **                %s\n",__DATE__);
	fprintf(stdout, "                          **    **                                     \n");
	fprintf(stdout, "        **    **          ******        ****      ********    ******** \n");
	fprintf(stdout, "        **    **          **    **    **    **    **          **       \n");
	fprintf(stdout, "        **    **          **    **    **    **      ****        ****   \n");
	fprintf(stdout, "        **  ****          **    **    **    **          **          ** \n");
	fprintf(stdout, "        ****  **  **    ********        ****      ********    ******** \n");
	fprintf(stdout, "        **    ****                                                     \n");
	fprintf(stdout, "        **                  %s\n",UBOSS_RELEASE);
	fprintf(stdout, "                            A Lightweight MicroService Framework       \n");
	fprintf(stdout, "\n\n");
	fprintf(stdout, "================================================================================\n");
}

int
main(int argc, char *argv[]) {
	// 输出 logo
	logo();

	const char * config_file = NULL ;

	// 执行必须有一个参数，否则打印错误信息。
	if (argc > 1) {
		config_file = argv[1];
	} else {
//		fprintf(stderr, "usage: uboss configfilename\n");
//		return 1;
		config_file = "uboss.conf";
	}

	fprintf(stdout, "Config File = %s\n", config_file);

//	luaS_initshr(); // 初始化 Lua 全局共享表
	uboss_globalinit(); // 全局初始化
	uboss_env_init(); // LUA环境初始化

	sigign(); // 信号处理

	// 声明 配置文件 的结构
	struct uboss_config config;

	struct lua_State *L = lua_newstate(uboss_lalloc, NULL); // 实例化一个 Lua VM 用于处理 配置文件
	luaL_openlibs(L);	// link lua lib 使用 Lua 标准库

	int err = luaL_loadstring(L, load_config); // 加载lua脚本字符串，即上面定义的 load_config 字符串
	assert(err == LUA_OK);
	lua_pushstring(L, config_file); // 将配置文件的名称压入栈

	err = lua_pcall(L, 1, 1, 0); // 以 config_file 为参数，调用 load_config 函数。
	if (err) {
		fprintf(stderr,"%s\n",lua_tostring(L,-1));
		lua_close(L); // 关闭 lua
		return 1;
	}
	_init_env(L); // 初始化 lua 环境

	config.root = optstring("root","./"); // 根目录
	config.thread =  optint("thread",8); // 启动工作线程数
	config.module_path = optstring("module","./module/?.so"); // C写的模块路径
	config.harbor = optint("harbor", 1); // 集群的编号
	config.bootstrap = optstring("bootstrap","luavm bootstrap"); // 启动脚本
	config.daemon = optstring("daemon", NULL); // 守护进程 pid 路径

	config.logservice = optstring("logservice", "logger"); // 日志记录器的服务
	config.logger = optstring("logger", NULL); // 日志记录器
	config.logpath = optstring("logpath", "./log/"); // 保存日志的路径


	lua_close(L); // 关闭 lua

	uboss_start(&config); // 启动 uBoss 框架
	uboss_globalexit(); // 退出全局初始化
//	luaS_exitshr(); // 退出 Lua 全局共享表


	return 0;
}
