/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Log
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#ifndef UBOSS_LOG_H
#define UBOSS_LOG_H

/*
 * 日志模块
 *
 * 如果开启，可记录日志到文件或网络中
*/

#include "uboss_env.h"
#include "uboss.h"

#include <stdio.h>
#include <stdint.h>

FILE * uboss_log_open(struct uboss_context * ctx, uint32_t handle);
void uboss_log_close(struct uboss_context * ctx, FILE *f, uint32_t handle);
void uboss_log_output(FILE *f, uint32_t source, int type, int session, void * buffer, size_t sz);


#endif
