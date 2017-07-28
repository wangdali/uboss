####################
#
# uBoss 编译脚本
#
####################


# 包含平台参数文件
include platform.mk

# uBoss 生成路径
UBOSS_BUILD_PATH ?= .

# uBoss 模块的路径
MODULE_PATH ?= module

# Lua 库的路径
LUA_LIB_PATH ?= lib

# 定义标志
CFLAGS = -g -O2 -Wall
# CFLAGS += -DUSE_PTHREAD_LOCK


###
# 编译 lua
###

# lua静态库的路径
LUA_STATICLIB := 3rd/lua/src/liblua.a
LUA_LIB ?= $(LUA_STATICLIB)
LUA_INC ?= 3rd/lua/src

$(LUA_STATICLIB) :
	cd 3rd/lua && $(MAKE) CC='$(CC) -std=gnu99' $(PLAT)

###
# uboss
###

# uBoss 的模块
MODULE = luavm logger

# Lua 的库
LUA_CLIB = uboss

# uBoss 核心
UBOSS_CORE = uboss.c uboss_env.c uboss_start.c uboss_handle.c uboss_module.c uboss_mq.c \
  uboss_server.c uboss_monitor.c uboss_malloc.c uboss_log.c uboss_command.c uboss_timer.c \
  uboss_context.c
   

all : \
  $(UBOSS_BUILD_PATH)/uboss \
  $(foreach v, $(MODULE), $(MODULE_PATH)/$(v).so) \
  $(foreach v, $(LUA_CLIB), $(LUA_LIB_PATH)/$(v).so) 

$(UBOSS_BUILD_PATH)/uboss : $(foreach v, $(UBOSS_CORE), core/$(v)) $(LUA_LIB) $(MALLOC_STATICLIB)
	$(CC) $(CFLAGS) -o $@ $^ -Icore -I$(LUA_INC) $(EXPORT) $(UBOSS_LIBS) 

$(LUA_LIB_PATH) :
	mkdir $(LUA_LIB_PATH)

$(MODULE_PATH) :
	mkdir $(MODULE_PATH)

###
# 编译 uBoss 模块
###
define MODULE_TEMP
  $$(MODULE_PATH)/$(1).so : module/$(1)/module_$(1).c | $$(MODULE_PATH)
	$$(CC) $$(CFLAGS) $$(SHARED) $$< -o $$@ -Icore -I$$(LUA_INC)
endef

$(foreach v, $(MODULE), $(eval $(call MODULE_TEMP,$(v))))

###
# 编译 lua 库
###
$(LUA_LIB_PATH)/uboss.so : lib/uboss/lua-uboss.c lib/uboss/lua-seri.c | $(LUA_LIB_PATH)
	$(CC) $(CFLAGS) $(SHARED) $^ -o $@ -Icore -Imodule -Ilib -I$(LUA_INC)

###
# 清理项目
###
clean :
	rm -f $(UBOSS_BUILD_PATH)/uboss $(MODULE_PATH)/*.so $(LUA_LIB_PATH)/*.so $(UBOSS_BUILD_PATH)/core/*.o

###
# 清理所有项目，包括 Lua 和 Jemalloc 项目
###
cleanall: clean
	cd 3rd/lua && $(MAKE) clean
	rm -f $(LUA_STATICLIB)

