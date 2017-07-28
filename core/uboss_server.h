/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Server
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#ifndef UBOSS_SERVER_H
#define UBOSS_SERVER_H

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>



// uBoss 的节点
struct uboss_node {
	int total; // 节点总数
	int init;
	uint32_t monitor_exit;
	pthread_key_t handle_key;
};

// 声明静态全局变量
// 由于静态全局变量只能在本文件中使用
// 所有暂时去掉静态，有待测试
//static struct uboss_node G_NODE;
struct uboss_node G_NODE;


void uboss_globalinit(void);
void uboss_globalexit(void);
void uboss_initthread(int m);

#endif
