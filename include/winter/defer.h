#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "error.h"
#include "utils.h"

typedef struct {
    void* arg;
    bool err;

    const char* file;
    const char* func;
    uint32_t line;
} defer_t;

#define defer(fun, args)                                                                                               \
    defer_t concat(defer_, __COUNTER__) __attribute__((unused, __cleanup__(defer_##fun))) = {                          \
        .arg = (void*)&args, .err = false, .line = __LINE__, .file = __FILE__, .func = __FUNCTION__                    \
    }

#define errdefer(fun, args)                                                                                            \
    defer_t concat(defer_, __COUNTER__) __attribute__((unused, __cleanup__(defer_##fun))) = {                          \
        .arg = (void*)&args, .err = true, .line = __LINE__, .file = __FILE__, .func = __FUNCTION__                     \
    }

#define defer_impl(name)                                                                                               \
    __attribute__((unused)) static inline void defer_##name(__attribute__((unused)) const defer_t* __defer)

#define defer_arg(type) ((type*)__defer->arg)

#define defer_guard()                                                                                                  \
    if (__defer->err && error_get_code() == 0)                                                                         \
    return

defer_impl(free) {
    defer_guard();
    free(*defer_arg(void*));
}
