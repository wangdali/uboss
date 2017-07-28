/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Atomic
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

// 原子操作
#ifndef UBOSS_ATOMIC_H
#define UBOSS_ATOMIC_H

// 比较与交换
#define ATOM_CAS(ptr, oval, nval) __sync_bool_compare_and_swap(ptr, oval, nval)
#define ATOM_CAS_POINTER(ptr, oval, nval) __sync_bool_compare_and_swap(ptr, oval, nval)

// 先加1再取回
#define ATOM_INC(ptr) __sync_add_and_fetch(ptr, 1)

// 先取回再加1
#define ATOM_FINC(ptr) __sync_fetch_and_add(ptr, 1)

// 先减1再取回
#define ATOM_DEC(ptr) __sync_sub_and_fetch(ptr, 1)

// 先取回再减1
#define ATOM_FDEC(ptr) __sync_fetch_and_sub(ptr, 1)

// 先加n再取回
#define ATOM_ADD(ptr,n) __sync_add_and_fetch(ptr, n)

// 先减n再取回
#define ATOM_SUB(ptr,n) __sync_sub_and_fetch(ptr, n)

// 先加n再取回
#define ATOM_AND(ptr,n) __sync_and_and_fetch(ptr, n)

#endif
