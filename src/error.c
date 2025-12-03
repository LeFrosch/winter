#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "winter/error.h"
#include "winter/utils.h"

#define MAX_ERRORS 32

_Thread_local static error_frame_t error_list[MAX_ERRORS];
_Thread_local static uint32_t error_count = 0;
_Thread_local static int32_t error_code = 0;

void
error_push(const char* file, const char* func, const uint32_t line, const int32_t code)
{
    assert(code != 0);

    error_code = code;

    if (error_count >= MAX_ERRORS) {
        return;
    }

    error_frame_t* frame = error_list + error_count++;
    frame->file = file;
    frame->func = func;
    frame->line = line;
    frame->code = code;
    frame->msg[0] = '\0';
}

__attribute__((__format__(__printf__, 1, 0))) void
error_append_message(const char* format, ...)
{
    assert(error_count > 0);

    error_frame_t* frame = error_trace_nth(error_count - 1);
    if (frame == nullptr) {
        return;
    }

    const size_t offset = strlen(frame->msg);
    if (offset >= sizeof(frame->msg)) {
        return;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(frame->msg + offset, sizeof(frame->msg) - offset, format, args);
    va_end(args);
}

int32_t
error_get_code(void)
{
    return error_code;
}

uint32_t
error_trace_length(void)
{
    return min(error_count, MAX_ERRORS);
}

error_frame_t*
error_trace_nth(const uint32_t n)
{
    if (n >= error_count || error_count >= MAX_ERRORS) {
        return nullptr;
    }

    return error_list + n;
}

void
error_clear(void)
{
    error_code = 0;
    error_count = 0;
}
