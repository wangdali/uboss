/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Text Command
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#ifndef UBOSS_COMMAND_H
#define UBOSS_COMMAND_H

#include <stdint.h>
#include <stdlib.h>

struct uboss_context;
struct uboss_message;
struct uboss_monitor;
const char * uboss_command(struct uboss_context * context, const char * cmd , const char * parm);

#endif
