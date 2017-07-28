/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Memory Malloc
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/
#ifndef UBOSS_MALLOC_H
#define UBOSS_MALLOC_H

#include <stddef.h>

#define uboss_malloc malloc
#define uboss_calloc calloc
#define uboss_realloc realloc
#define uboss_free free

// for uboss_lalloc use
#define raw_realloc realloc
#define raw_free free


void * uboss_lalloc(void *ud, void *ptr, size_t osize, size_t nsize);	// use for lua

#endif /* UBOSS_MALLOC_H */
