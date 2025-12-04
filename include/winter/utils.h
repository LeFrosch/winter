#pragma once

#define concat_(a, b) a##b
#define concat(a, b) concat_(a, b)

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define try_alloc(ptr, size)                                                                                           \
    do {                                                                                                               \
        ptr = malloc(size);                                                                                            \
        if (ptr == nullptr) {                                                                                          \
            failure(ENOMEM, msg("no memory for: " #ptr), with_int(size));                                              \
        }                                                                                                              \
        memset(ptr, 0, size);                                                                                          \
    } while (0)
