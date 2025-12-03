MODULE_PATH := $(dir $(lastword $(MAKEFILE_LIST)))

WINTER_SRCS := $(addprefix $(MODULE_PATH)src/, error.c)
WINTER_HDRS := $(wildcard $(MODULE_PATH)include/**/*.h) 

UNAME := $(shell uname -s)

# configure sanitizers for the platform
ifeq ($(UNAME), Darwin)
    SANITIZER := -fsanitize=address,undefined
else ifeq ($(UNAME), Linux)
    SANITIZER := -fsanitize=address,undefined,leak
else
    $(error unsupported platform)
endif

WINTER_CFLAGS      := -I$(MODULE_PATH)include
WINTER_DBG_CFLAGS  := -g -DDEBUG -O0 $(SANITIZER)
WINTER_TEST_CFLAGS := -DWINTER_TEST $(WINTER_DBG_CFLAGS)
