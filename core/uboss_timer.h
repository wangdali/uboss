/*
** Copyright (c) 2014-2016 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Timer
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

/*
 * 定时器模块
 *
 * 提供定时器事件链表，管理定时器，并到期调用回调函数
*/
#ifndef UBOSS_TIMER_H
#define UBOSS_TIMER_H

#include <stdint.h>

int uboss_timeout(uint32_t handle, int time, int session);
void uboss_updatetime(void);
uint32_t uboss_starttime(void);
uint64_t uboss_thread_time(void);	// for profile, in micro second

void uboss_timer_init(void);

#endif
