/*
** Copyright (c) 2014-2017 uboss.org All rights Reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Lua Binding uboss Library
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"
#include "lua-seri.h"

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct snlua {
	lua_State * L;
	struct uboss_context * ctx;
	const char * preload;
};

// 跟踪函数
static int
traceback (lua_State *L) {
	const char *msg = lua_tostring(L, 1); // 获得消息字符串
	if (msg)
		luaL_traceback(L, L, msg, 1); // 跟踪
	else {
		lua_pushliteral(L, "(no error message)");
	}
	return 1;
}

// 回调函数
static int
_cb(struct uboss_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	lua_State *L = ud;
	int trace = 1;
	int r;
	int top = lua_gettop(L);
	if (top == 0) {
		lua_pushcfunction(L, traceback); // 压入跟踪函数
		lua_rawgetp(L, LUA_REGISTRYINDEX, _cb);
	} else {
		assert(top == 2);
	}
	lua_pushvalue(L,2);

	lua_pushinteger(L, type); // 压入类型
	lua_pushlightuserdata(L, (void *)msg); // 压入用户数据 消息
	lua_pushinteger(L,sz); // 压入消息长度
	lua_pushinteger(L, session); // 压入会话
	lua_pushinteger(L, source); // 压入来源地址

	r = lua_pcall(L, 5, 0 , trace); // 调用函数

	if (r == LUA_OK) {
		return 0;
	}

	// 注册上下文
	const char * self = uboss_command(context, "REG", NULL);
	switch (r) {
	case LUA_ERRRUN:
		uboss_error(context, "lua call [%x to %s : %d msgsz = %d] error : " KRED "%s" KNRM, source , self, session, sz, lua_tostring(L,-1));
		break;
	case LUA_ERRMEM:
		uboss_error(context, "lua memory error : [%x to %s : %d]", source , self, session);
		break;
	case LUA_ERRERR:
		uboss_error(context, "lua error in error : [%x to %s : %d]", source , self, session);
		break;
	case LUA_ERRGCMM:
		uboss_error(context, "lua gc error : [%x to %s : %d]", source , self, session);
		break;
	};

	lua_pop(L,1);

	return 0;
}

static int
forward_cb(struct uboss_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	_cb(context, ud, type, session, source, msg, sz);
	// don't delete msg in forward mode.
	return 1;
}

// lua 的返回函数
static int
lcallback(lua_State *L) {
	struct uboss_context * context = lua_touserdata(L, lua_upvalueindex(1)); // 从用户数据获得上下文结构
	int forward = lua_toboolean(L, 2);
	luaL_checktype(L,1,LUA_TFUNCTION); // 检查类型是否为 函数
	lua_settop(L,1);
	lua_rawsetp(L, LUA_REGISTRYINDEX, _cb);

	lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
	lua_State *gL = lua_tothread(L,-1);

	// 调用uboss回调函数，判断模式
	if (forward) {
		uboss_callback(context, gL, forward_cb);
	} else {
		uboss_callback(context, gL, _cb);
	}

	return 0;
}

// lua 命令
static int
lcommand(lua_State *L) {
	struct uboss_context * context = lua_touserdata(L, lua_upvalueindex(1)); // 从用户数据获得上下文结构
	const char * cmd = luaL_checkstring(L,1); // 检查命令是否为字符串
	const char * result;
	const char * parm = NULL;
	if (lua_gettop(L) == 2) {
		parm = luaL_checkstring(L,2); // 检查参数是否为字符串
	}

	// 执行 uboss 命令
	result = uboss_command(context, cmd, parm);
	if (result) {
		lua_pushstring(L, result); // 压入结果到lua
		return 1;
	}
	return 0;
}

// lua 整数型命令
static int
lintcommand(lua_State *L) {
	struct uboss_context * context = lua_touserdata(L, lua_upvalueindex(1)); // 从用户数据获得上下文结构
	const char * cmd = luaL_checkstring(L,1); // 检查命令是否为字符串
	const char * result;
	const char * parm = NULL;
	char tmp[64];	// for integer parm
	if (lua_gettop(L) == 2) {
		int32_t n = (int32_t)luaL_checkinteger(L,2); // 检查参数是否为整数
		sprintf(tmp, "%d", n); // 转换整数为字符串
		parm = tmp;
	}

	// 执行 uboss 命令
	result = uboss_command(context, cmd, parm);
	if (result) {
		lua_Integer r = strtoll(result, NULL, 0);
		lua_pushinteger(L, r); // 压入结果到lua
		return 1;
	}
	return 0;
}

// 获得服务的会话ID
static int
lgenid(lua_State *L) {
	struct uboss_context * context = lua_touserdata(L, lua_upvalueindex(1)); // 从用户数据获得上下文结构

	// 发送消息给自己，uboss框架会返回会话ID
	int session = uboss_send(context, 0, 0, PTYPE_TAG_ALLOCSESSION , 0 , NULL, 0); // uboss 发送消息
	lua_pushinteger(L, session); // 压入会话ID
	return 1;
}

// 获得目标地址
static const char *
get_dest_string(lua_State *L, int index) {
	const char * dest_string = lua_tostring(L, index);
	if (dest_string == NULL) {
		luaL_error(L, "dest address type (%s) must be a string or number.", lua_typename(L, lua_type(L,index)));
	}
	return dest_string;
}

/*
	uint32 address
	 string address
	integer type
	integer session
	string message
	 lightuserdata message_ptr
	 integer len
 */
static int
lsend(lua_State *L) {
	struct uboss_context * context = lua_touserdata(L, lua_upvalueindex(1)); // 从用户数据获得上下文结构
	uint32_t dest = (uint32_t)lua_tointeger(L, 1); // 获得目的地的句柄值
	const char * dest_string = NULL;
	if (dest == 0) {
		if (lua_type(L,1) == LUA_TNUMBER) {
			return luaL_error(L, "Invalid service address 0");
		}
		dest_string = get_dest_string(L, 1); // 获得目标地址的字符串
	}

	int type = luaL_checkinteger(L, 2); // 检查类型是否为整数类型
	int session = 0;
	if (lua_isnil(L,3)) {
		type |= PTYPE_TAG_ALLOCSESSION;
	} else {
		session = luaL_checkinteger(L,3); // 获得会话的ID
	}

	int mtype = lua_type(L,4);
	switch (mtype) {
	// 字符串形式
	case LUA_TSTRING: {
		size_t len = 0;
		void * msg = (void *)lua_tolstring(L,4,&len); // 获得消息字符串
		if (len == 0) {
			msg = NULL;
		}
		if (dest_string) {
			session = uboss_sendname(context, 0, dest_string, type, session , msg, len); // 以服务名字发送消息
		} else {
			session = uboss_send(context, 0, dest, type, session , msg, len); // 以服务句柄发送消息
		}
		break;
	}
	// Lua 用户数据形式
	case LUA_TLIGHTUSERDATA: {
		void * msg = lua_touserdata(L,4); // 获得消息数据
		int size = luaL_checkinteger(L,5); // 获得消息长度
		if (dest_string) {
			session = uboss_sendname(context, 0, dest_string, type | PTYPE_TAG_DONTCOPY, session, msg, size); // 以服务名字发送消息
		} else {
			session = uboss_send(context, 0, dest, type | PTYPE_TAG_DONTCOPY, session, msg, size); // 以服务句柄发送消息
		}
		break;
	}
	default:
		luaL_error(L, "uboss.send invalid param %s", lua_typename(L, lua_type(L,4)));
	}
	if (session < 0) {
		// send to invalid address
		// todo: maybe throw an error would be better
		return 0;
	}
	lua_pushinteger(L,session); // 压入会话ID
	return 1;
}

// 重定向消息
static int
lredirect(lua_State *L) {
	struct uboss_context * context = lua_touserdata(L, lua_upvalueindex(1)); // 从用户数据获得上下文结构
	uint32_t dest = (uint32_t)lua_tointeger(L,1); // 获得目的地的句柄值
	const char * dest_string = NULL;
	if (dest == 0) {
		dest_string = get_dest_string(L, 1); // 获得目标地址的字符串
	}
	uint32_t source = (uint32_t)luaL_checkinteger(L,2); // 获得并检查来源地址是否为整数
	int type = luaL_checkinteger(L,3); // 获得类型
	int session = luaL_checkinteger(L,4); // 获得会话ID

	int mtype = lua_type(L,5);
	switch (mtype) {

	// 字符串类型
	case LUA_TSTRING: {
		size_t len = 0;
		void * msg = (void *)lua_tolstring(L,5,&len); // 获得消息数据
		if (len == 0) {
			msg = NULL;
		}
		if (dest_string) {
			session = uboss_sendname(context, source, dest_string, type, session , msg, len); // 以服务名字发送消息
		} else {
			session = uboss_send(context, source, dest, type, session , msg, len); // 以服务句柄发送消息
		}
		break;
	}

	// Lua 用户数据类型
	case LUA_TLIGHTUSERDATA: {
		void * msg = lua_touserdata(L,5); // 从用户数据中获得消息数据
		int size = luaL_checkinteger(L,6); // 获得消息数据的长度
		if (dest_string) {
			session = uboss_sendname(context, source, dest_string, type | PTYPE_TAG_DONTCOPY, session, msg, size); // 以服务名字发送消息
		} else {
			session = uboss_send(context, source, dest, type | PTYPE_TAG_DONTCOPY, session, msg, size); // 以服务句柄发送消息
		}
		break;
	}
	default:
		luaL_error(L, "uboss.redirect invalid param %s", lua_typename(L,mtype));
	}
	return 0;
}

// 错误
static int
lerror(lua_State *L) {
	struct uboss_context * context = lua_touserdata(L, lua_upvalueindex(1)); // 从用户数据获得上下文结构
	int n = lua_gettop(L);
	if (n <= 1) {
		lua_settop(L, 1);
		const char * s = luaL_tolstring(L, 1, NULL);
		uboss_error(context, "%s", s);
		return 0;
	}
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	int i;
	for (i=1; i<=n; i++) {
		luaL_tolstring(L, i, NULL);
		luaL_addvalue(&b);
		if (i<n) {
			luaL_addchar(&b, ' ');
		}
	}
	luaL_pushresult(&b);
	uboss_error(context, "%s", lua_tostring(L, -1));
	return 0;
}

// 转换为字符串
static int
ltostring(lua_State *L) {
	if (lua_isnoneornil(L,1)) {
		return 0;
	}
	char * msg = lua_touserdata(L,1); // 从用户数据中获得消息数据
	int sz = luaL_checkinteger(L,2); // 获得消息数据的长度
	lua_pushlstring(L,msg,sz); // 压入字符串消息
	return 1;
}

// 远程消息
static int
lharbor(lua_State *L) {
	struct uboss_context * context = lua_touserdata(L, lua_upvalueindex(1)); // 从用户数据获得上下文结构
	uint32_t handle = (uint32_t)luaL_checkinteger(L,1); // 获得并检查句柄数据
	int harbor = 0;
	int remote = uboss_isremote(context, handle, &harbor); // 判断句柄是否为远程消息
	lua_pushinteger(L,harbor); // 压入 远程节点ID
	lua_pushboolean(L, remote); // 压入 是否远程消息的标志

	return 2;
}

static int
lpackstring(lua_State *L) {
	luaseri_pack(L); // lua序列化封包
	char * str = (char *)lua_touserdata(L, -2); // 从用户数据中获得字符串数据
	int sz = lua_tointeger(L, -1); // 获得数据的长度
	lua_pushlstring(L, str, sz); // 压入数据
	uboss_free(str); // 是否字符串空间
	return 1;
}

// 垃圾
static int
ltrash(lua_State *L) {
	int t = lua_type(L,1); // 获得类型
	switch (t) {
	// 字符串类型
	case LUA_TSTRING: {
		break;
	}
	// 用户数据类型
	case LUA_TLIGHTUSERDATA: {
		void * msg = lua_touserdata(L,1);
		luaL_checkinteger(L,2);
		uboss_free(msg);
		break;
	}
	default:
		luaL_error(L, "uboss.trash invalid param %s", lua_typename(L,t));
	}

	return 0;
}

// 获得当前时间
static int
lnow(lua_State *L) {
	uint64_t ti = uboss_now(); // uboss 获得当前时间
	lua_pushinteger(L, ti); // 压入时间
	return 1;
}

int
luaopen_uboss_core(lua_State *L) {
	luaL_checkversion(L);

	luaL_Reg l[] = {
		{ "send" , lsend },
		{ "genid", lgenid },
		{ "redirect", lredirect },
		{ "command" , lcommand },
		{ "intcommand", lintcommand },
		{ "error", lerror },
		{ "tostring", ltostring },
		{ "harbor", lharbor },
		{ "pack", luaseri_pack },
		{ "unpack", luaseri_unpack },
		{ "packstring", lpackstring },
		{ "trash" , ltrash },
		{ "callback", lcallback },
		{ "now", lnow },
		{ NULL, NULL },
	};

	luaL_newlibtable(L, l);

	lua_getfield(L, LUA_REGISTRYINDEX, "uboss_context");
	struct uboss_context *ctx = lua_touserdata(L,-1);
	if (ctx == NULL) {
		return luaL_error(L, "Init uboss context first");
	}

	luaL_setfuncs(L,l,1);

	return 1;
}
