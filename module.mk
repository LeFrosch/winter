MODULE_PATH := $(dir $(lastword $(MAKEFILE_LIST)))

WINTER_SRCS := $(addprefix $(MODULE_PATH)src/, error.c)
WINTER_HDRS := $(wildcard $(MODULE_PATH)include/**/*.h) 

# configure sanitizers for the platform
ifeq ($(shell uname -s), Darwin)
    SANITIZER := -fsanitize=address,undefined
else ifeq ($(shell uname -s), Linux)
    SANITIZER := -fsanitize=address,undefined,leak
else
    $(error unsupported platform)
endif

WINTER_FLAGS      := -I$(MODULE_PATH)include
WINTER_DBG_FLAGS  := -g -DDEBUG -O0 $(SANITIZER)
WINTER_TEST_FLAGS := -DWINTER_TEST $(WINTER_DBG_FLAGS)
