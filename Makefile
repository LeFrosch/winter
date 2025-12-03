CC     := clang
FORMAT := clang-format
CFLAGS := -Wall -Wextra -Werror -std=c23 -O3

include module.mk

CFLAGS += $(WINTER_CFLAGS) $(WINTER_TEST_CFLAGS)

SRCS := $(addprefix src/, test.c) $(WINTER_SRCS)
HDRS := $(WINTER_HDRS)
OBJS := $(SRCS:%.c=build/%.o)

build/%.o: %.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c -o $@ $<

test: $(OBJS)
	$(CC) $(SANITIZER) $^ -o $@

clean:
	rm -rf build test

format:
	$(FORMAT) -i $(SRCS) $(HDRS)

.PHONY: clean format
