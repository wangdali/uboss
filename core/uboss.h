/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Main Function
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in this file end.
*/

#ifndef UBOSS_H
#define UBOSS_H

#include "uboss_malloc.h"

#include <stddef.h>
#include <stdint.h>

#define UBOSS_VERSION_MAJOR	"3"
#define UBOSS_VERSION_MINOR	"0"
#define UBOSS_VERSION_RELEASE	"0"
#define UBOSS_VERSION_NUM	300

#define UBOSS_VERSION	"uBoss " UBOSS_VERSION_MAJOR "." UBOSS_VERSION_MINOR
#define UBOSS_RELEASE	UBOSS_VERSION "." UBOSS_VERSION_RELEASE
#define UBOSS_COPYRIGHT	UBOSS_RELEASE "  Copyright (c) 2014-2017 uboss.org  All rights reserved."
#define UBOSS_AUTHORS	"Dali Wang <dali@uboss.org>"

#define PTYPE_TEXT 0 // 文本类型
#define PTYPE_RESPONSE 1 // 响应类型
#define PTYPE_MULTICAST 2 // 组播类型
#define PTYPE_CLIENT 3 // 客户端类型
#define PTYPE_SYSTEM 4 // 系统类型
#define PTYPE_HARBOR 5 // 集群类型
#define PTYPE_SOCKET 6 // 网络类型
#define PTYPE_ERROR 7 // 错误类型
#define PTYPE_RESERVED_QUEUE 8 // 队列保留类型
#define PTYPE_RESERVED_DEBUG 9 // 调试保留类型
#define PTYPE_RESERVED_LUA 10 // Lua保留类型
#define PTYPE_RESERVED_SNAX 11 // SNAX保留类型

#define PTYPE_TAG_DONTCOPY 0x10000 // 不复制消息
#define PTYPE_TAG_ALLOCSESSION 0x20000 // 允许会话

struct uboss_context;

void uboss_error(struct uboss_context * context, const char *msg, ...);
const char * uboss_command(struct uboss_context * context, const char * cmd , const char * parm);
uint32_t uboss_queryname(struct uboss_context * context, const char * name);
int uboss_send(struct uboss_context * context, uint32_t source, uint32_t destination , int type, int session, void * msg, size_t sz);
int uboss_sendname(struct uboss_context * context, uint32_t source, const char * destination , int type, int session, void * msg, size_t sz);

int uboss_isremote(struct uboss_context *, uint32_t handle, int * harbor);

typedef int (*uboss_cb)(struct uboss_context * context, void *ud, int type, int session, uint32_t source , const void * msg, size_t sz);
void uboss_callback(struct uboss_context * context, void *ud, uboss_cb cb);

uint32_t uboss_current_handle(void);
uint64_t uboss_now(void);
void uboss_debug_memory(const char *info);	// for debug use, output current service memory to stderr
char * uboss_strdup(const char *str);
#endif /* UBOSS_H */
/*
The MIT License (MIT)

Copyright (c) 2014-2017 uboss.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
 */
