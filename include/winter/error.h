#pragma once

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#define result_t __attribute__((warn_unused_result)) int32_t

#define SUCCESS 0
#define FAILURE 1

typedef struct {
    const char* file;
    const char* func;
    uint32_t line;

    int32_t code;
    char msg[128];
} error_frame_t;

/// Reports a thread local error.
void
error_push(const char* file, const char* func, uint32_t line, int32_t code);

/// Appends a message to the last reported error by the current thread.
__attribute__((__format__(__printf__, 1, 0))) void
error_append_message(const char* format, ...);

/// Returns the last error code reported by the current thread.
int32_t
error_get_code(void);

/// Returns the length of the error trace for the current thread.
uint32_t
error_trace_length(void);

/// Gets the nth frame of the error trace or null if the index is out of bounds.
error_frame_t*
error_trace_nth(uint32_t n);

/// Clears the error state and the trace for the current thread.
void
error_clear(void);

#define ecode error_get_code()

#define failure(code, ...)                                                                                             \
    do {                                                                                                               \
        error_push(__FILE__, __FUNCTION__, __LINE__, code);                                                            \
        (void)(__VA_ARGS__);                                                                                           \
        return FAILURE;                                                                                                \
    } while (0)

#define ensure(expr, ...)                                                                                              \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            failure(EINVAL, msg("ensure %s", #expr) __VA_OPT__(, __VA_ARGS__));                                        \
        }                                                                                                              \
    } while (0)

#define try(expr, ...)                                                                                                 \
    do {                                                                                                               \
        if ((expr) != SUCCESS) {                                                                                       \
            failure(error_get_code(), msg("try %s", #expr) __VA_OPT__(, __VA_ARGS__));                                 \
        }                                                                                                              \
    } while (0)

#define handle(expr)                                                                                                   \
    for (struct {                                                                                                      \
             const char* expr_str;                                                                                     \
             int32_t result;                                                                                           \
         } __state = { #expr, expr };                                                                                  \
         __state.result != SUCCESS;                                                                                    \
         error_clear(), __state.result = SUCCESS)

#define forward(...) failure(error_get_code(), msg("handler for: %s", __state.expr_str) __VA_OPT__(, __VA_ARGS__))

#define msg(msg, ...) error_append_message(msg __VA_OPT__(, __VA_ARGS__))

#define with_int(expr) error_append_message(", %s = %ji", #expr, (intmax_t)(expr))

#define with_uint(expr) error_append_message(", %s = %ju", #expr, (uintmax_t)(expr))

#define with_dbl(expr) error_append_message(", %s = %Lg", #expr, (long double)(expr))

#define with_ptr(expr) error_append_message(", %s = %p", #expr, (void*)(expr))

#define with_str(expr) error_append_message(", %s = %s", #expr, expr)
