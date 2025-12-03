CC     := clang
FORMAT := clang-format
FLAGS  := -Wall -Wextra -Werror -std=c23 -O3

include module.mk

FLAGS += $(WINTER_FLAGS) $(WINTER_TEST_FLAGS)

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
	find . -type f \( -name "*.c" -o -name "*.h" \) | xargs $(FORMAT) -i

.PHONY: clean format
