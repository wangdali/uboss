/*
** Copyright (c) 2014-2017 uboss.org All rights reserved.
** uBoss - A Lightweight MicroService Framework
**
** uBoss Service Handle
**
** Dali Wang<dali@uboss.org>
** See Copyright Notice in uboss.h
*/

#include "uboss.h"

#include "uboss_lock.h"
#include "uboss_handle.h"
#include "uboss_server.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define DEFAULT_SLOT_SIZE 4 // 默认槽的大小
#define MAX_SLOT_SIZE 0x40000000 // 最大的槽大小值

// 句柄的结构
struct handle_name {
	char * name; // 句柄名字
	uint32_t handle; // 句柄的值
};

// 句柄存储
struct handle_storage {
	struct rwlock lock; // 读写锁

	uint32_t harbor; // 集群ID
	uint32_t handle_index; // 句柄引索
	int slot_size; // 句柄的槽
	struct uboss_context ** slot; // uBoss 上下文的指针

	int name_cap; // 名字的最大数
	int name_count; // 名字总数
	struct handle_name *name; // 句柄的结构
};

// 声明一个全局的变量
static struct handle_storage *H = NULL;

// 注册句柄 - 注册一个 uBoss 上下文，并返回句柄值
uint32_t
uboss_handle_register(struct uboss_context *ctx) {
	struct handle_storage *s = H; // 获得全局变量

	rwlock_wlock(&s->lock); // 写锁

	// 因为在死循环中，当找不到空的槽分配句柄时，在放大两倍槽空间时。
	// 会从新进入句柄匹配槽的过程，直到找到空槽或者槽满到最大值。
	for (;;) {
		int i;
		// 循环所有槽
		for (i=0;i<s->slot_size;i++) {
			// 获得最后的句柄值 加上 i的值 与上句柄的掩码
			// 限制生成的 句柄值 只能在句柄的掩码范围之内
			// 最多有 0xFFFFFF（1600W） 个句柄，每个句柄占4字节，即最大需要64MB内存空间
			uint32_t handle = (i+s->handle_index) & HANDLE_MASK;

			// 散列值 = 句柄值与上句柄槽的值，用于快速找到对应的槽，避免越界。
			// 如果句柄值大于 槽的值，散列值会回滚到前面，这样找到的数组元素就已经被占用。
			// 从而找不到空的槽，则跳过返回这个句柄，并把槽放大两倍，再分配句柄。
			int hash = handle & (s->slot_size-1);

			// 找到一个空的槽，并将 上下文结构 赋给这个槽
			if (s->slot[hash] == NULL) { // 如果槽为空
				s->slot[hash] = ctx; // 将上下文放入槽中
				s->handle_index = handle + 1; // 句柄引索加一

				rwlock_wunlock(&s->lock); // 写解锁

				handle |= s->harbor; // 句柄或上集群ID的值，生成新的句柄值
				return handle; // 返回新的句柄值
			}
		}

		// 如果循环完所有槽，都找不到空槽，则将槽的总数扩大2倍
		assert((s->slot_size*2 - 1) <= HANDLE_MASK); // 断言槽的总数不大于 HANDLE_MASK ，即0xffffff。
		struct uboss_context ** new_slot = uboss_malloc(s->slot_size * 2 * sizeof(struct uboss_context *)); // 分配槽的两倍内存空间
		memset(new_slot, 0, s->slot_size * 2 * sizeof(struct uboss_context *)); // 初始化内存空间为空

		// 循环原来所有的槽，将值移到扩大2倍的槽中
		for (i=0;i<s->slot_size;i++) { // 循环所有槽
			int hash = uboss_context_handle(s->slot[i]) & (s->slot_size * 2 - 1);
			assert(new_slot[hash] == NULL);
			new_slot[hash] = s->slot[i]; // 将旧槽中的值赋给新槽
		}
		uboss_free(s->slot); // 释放旧槽的空间
		s->slot = new_slot; // 替换成新槽的地址
		s->slot_size *= 2; // 修改槽的大小为原来的2倍
	} // 死循环 for(;;)

	// 只有意外的情况下才会执行到这里。
	// 注意：没有返回值，最好返回0，但可能会重复
	return 0;
}

// 收回句柄
int
uboss_handle_retire(uint32_t handle) {
	int ret = 0;
	struct handle_storage *s = H; // 获得全局变量

	rwlock_wlock(&s->lock); // 写锁

	uint32_t hash = handle & (s->slot_size-1); // 根据 句柄值 计算出散列值
	struct uboss_context * ctx = s->slot[hash]; // 根据 散列值 从槽数组中取出 上下文结构

	// 如果取出的 上下文结构 不为空，且其句柄值相等
	if (ctx != NULL && uboss_context_handle(ctx) == handle) {
		s->slot[hash] = NULL; // 清空 散列值 对应的槽
		ret = 1;
		int i;
		int j=0, n=s->name_count;
		for (i=0; i<n; ++i) {
			if (s->name[i].handle == handle) {
				uboss_free(s->name[i].name); // 释放内存空间
				continue;
			} else if (i!=j) {
				s->name[j] = s->name[i];
			}
			++j;
		}
		s->name_count = j;
	} else {
		ctx = NULL;
	}

	rwlock_wunlock(&s->lock); // 写解锁

	if (ctx) {
		// release ctx may call uboss_handle_* , so wunlock first.
		uboss_context_release(ctx); // 释放 上下文结构
	}

	return ret;
}

// 回收所有句柄
void
uboss_handle_retireall() {
	struct handle_storage *s = H;
	for (;;) {
		int n=0;
		int i;

		// 循环所有槽
		for (i=0;i<s->slot_size;i++) {
			rwlock_rlock(&s->lock); // 读锁
			struct uboss_context * ctx = s->slot[i];
			uint32_t handle = 0;
			if (ctx)
				handle = uboss_context_handle(ctx); // 从 上下文句柄 中获取其句柄值
			rwlock_runlock(&s->lock);
			if (handle != 0) {
				if (uboss_handle_retire(handle)) { // 回收句柄
					++n;
				}
			}
		}
		if (n==0)
			return;
	}
}

// 取出 句柄值 对应的上下文结构，并引用加一
struct uboss_context *
uboss_handle_grab(uint32_t handle) {
	struct handle_storage *s = H; // 获得 全局变量
	struct uboss_context * result = NULL;

	rwlock_rlock(&s->lock); // 读锁

	uint32_t hash = handle & (s->slot_size-1); // 根据 句柄值 计算散列值
	struct uboss_context * ctx = s->slot[hash]; // 根据 散列值 中取出对应 上下文结构
	if (ctx && uboss_context_handle(ctx) == handle) {
		result = ctx; // 设置返回的 上下文结构 的指针
		uboss_context_grab(result); // 上下文结构 引用计数 加一
	}

	rwlock_runlock(&s->lock); // 读解锁

	return result; // 返回 上下文结构 的指针
}

// 根据名字查找句柄值
uint32_t
uboss_handle_findname(const char * name) {
	struct handle_storage *s = H; // 获得 全局变量

	rwlock_rlock(&s->lock); // 读锁

	uint32_t handle = 0; // 初始化句柄值为0

	int begin = 0;
	int end = s->name_count - 1;

	// 折半算法查找
	while (begin<=end) {
		int mid = (begin+end)/2;
		struct handle_name *n = &s->name[mid];
		int c = strcmp(n->name, name); // 比较名字
		if (c==0) {
			handle = n->handle;
			break;
		}
		if (c<0) {
			begin = mid + 1;
		} else {
			end = mid - 1;
		}
	}

	rwlock_runlock(&s->lock); // 读解锁

	return handle;
}

// 在之前插入名字
static void
_insert_name_before(struct handle_storage *s, char *name, uint32_t handle, int before) {
	// 当 总数 大于等于 最大值
	if (s->name_count >= s->name_cap) {
		s->name_cap *= 2; // 最大名字数量值 放大2倍
		assert(s->name_cap <= MAX_SLOT_SIZE);
		struct handle_name * n = uboss_malloc(s->name_cap * sizeof(struct handle_name)); // 重新分配内存空间

		int i;
		// 循环
		for (i=0;i<before;i++) {
			n[i] = s->name[i]; // 将至赋给新空间
		}

		// 循环所有名字
		for (i=before;i<s->name_count;i++) {
			n[i+1] = s->name[i];
		}
		uboss_free(s->name); // 释放旧空间
		s->name = n;
	} else {
		// 还有空间可以存放新的名字和句柄对应值时
		int i;
		for (i=s->name_count;i>before;i--) {
			s->name[i] = s->name[i-1];
		}
	}
	s->name[before].name = name; // 赋值 名字到新的空间
	s->name[before].handle = handle; // 赋值 名字对应的句柄值到新的空间
	s->name_count ++; // 总数 加一
}

// 插入名字
static const char *
_insert_name(struct handle_storage *s, const char * name, uint32_t handle) {
	int begin = 0;
	int end = s->name_count - 1;
	while (begin<=end) {
		int mid = (begin+end)/2;
		struct handle_name *n = &s->name[mid];
		int c = strcmp(n->name, name); // 比较名字
		if (c==0) {
			return NULL;
		}
		if (c<0) {
			begin = mid + 1;
		} else {
			end = mid - 1;
		}
	}
	char * result = uboss_strdup(name); // 分配空间并返回字符串指针

	_insert_name_before(s, result, handle, begin);

	return result;
}

// 关联名字与句柄值
const char *
uboss_handle_namehandle(uint32_t handle, const char *name) {
	rwlock_wlock(&H->lock); // 写锁

	const char * ret = _insert_name(H, name, handle); // 给句柄 插入 名字

	rwlock_wunlock(&H->lock); // 写解锁

	return ret;
}

// 初始化句柄
void
uboss_handle_init(int harbor) {
	assert(H==NULL); // 判断 全局变量 为空
	struct handle_storage * s = uboss_malloc(sizeof(*H)); // 分配 全局变量 的内存空间
	s->slot_size = DEFAULT_SLOT_SIZE; // 设置槽的默认大小
	s->slot = uboss_malloc(s->slot_size * sizeof(struct uboss_context *)); // 分配槽的内存空间
	memset(s->slot, 0, s->slot_size * sizeof(struct uboss_context *)); // 清空槽的内存空间

	rwlock_init(&s->lock); // 初始化 读写锁
	// reserve 0 for system
	s->harbor = (uint32_t) (harbor & 0xff) << HANDLE_REMOTE_SHIFT; // 集群ID
	s->handle_index = 1; // 句柄的引索
	s->name_cap = 2; // 名字的最大值
	s->name_count = 0; // 名字的总数
	s->name = uboss_malloc(s->name_cap * sizeof(struct handle_name)); // 分配名字的内存空间

	H = s; // 设置 全局变量 的值

	// 不要释放 H 结构的内存空间
	// Don't need to free H
}

