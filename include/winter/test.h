/*
 * Winter, a testing library
 *
 * Copyright (c) 2025 Daniel Brauner
 *
 * This work is derived from and inspired by:
 *   Snow, a testing library — https://github.com/mortie/snow
 *   Copyright (c) 2018 Martin Dørum
 *
 * Licensed under the MIT License:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <fnmatch.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "error.h"

#include <stdatomic.h>

#define WINTER_VERSION "0.0.1"

#define WINTER_INDENT "    "

#define WINTER_DEFAULT_TIMEOUT_MS 2000
#define WINTER_PROCESS_POLL_MS 5

#define WINTER_COLOR_BOLD "\033[1m"
#define WINTER_COLOR_RESET "\033[0m"
#define WINTER_COLOR_SUCCESS "\033[32m"
#define WINTER_COLOR_FAIL "\033[31m"
#define WINTER_COLOR_MAYBE "\033[35m"
#define WINTER_COLOR_DESC "\033[33m"

#define WINTER_FUNC __attribute__((unused)) static

#define WINTER_FUNC_INFO 0
#define WINTER_FUNC_BEFORE_EACH 1
#define WINTER_FUNC_AFTER_EACH 2

#define WINTER_EXIT_FAILURE 255

enum {
    _WINTER_OPT_VERSION,
    _WINTER_OPT_HELP,
    _WINTER_OPT_LIST,
    _WINTER_OPT_COLOR,
    _WINTER_OPT_DEBUG,
    _WINTER_OPT_RERUN,
    _WINTER_OPT_TIMEOUT,
    _WINTER_OPT_LAST,
};

typedef struct {
    size_t element_size;
    char* elements;
    size_t length;
    size_t allocated;
} winter_array_t;

typedef struct {
    const char* name;
    uint64_t id;
    uint16_t threads;
    double timeout;
} winter_test_t;

typedef struct {
    const char* name;
    winter_array_t tests;
    void (*func)(uint64_t, winter_array_t*);
} winter_suite_t;

typedef struct {
    bool initialized;

    winter_array_t suites;
    winter_array_t patterns;

    struct {
        pthread_mutex_t mutex;
        FILE* file;
    } print;

    struct {
        bool list;
        bool color;
        bool rerun;
        bool timeout;
    } opts;

    struct {
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        uint16_t threads;
        uint16_t waiting;
        uint64_t generation;
    } synchronization;
} winter_t;

/// Global state, shared between threads and resources.
extern winter_t _winter;

typedef struct {
    const char* filename;
    uint32_t linenum;

    uint16_t thread_id;
} winter_local_t;

/// Thread local state, private to each thread.
extern _Thread_local winter_local_t _winter_local;

typedef struct {
    double start_time;

    const winter_suite_t* suite;
    const winter_test_t* test;
} winter_unit_t;

typedef struct {
    winter_unit_t* unit;
    uint16_t thread_id;
} winter_args_t;

typedef struct {
    const char* name;
    char short_name;
    bool is_flag;
    bool overwritten;
    union {
        bool bool_val;
        const char* str_val;
    };
} _winter_opt_t;

WINTER_FUNC __attribute__((noreturn, __format__(__printf__, 1, 2))) void
_winter_fatal_error(const char* fmt, ...) {
    fprintf(stderr, "Fatal error: ");

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, ".\n");

    exit(EXIT_FAILURE);
}

WINTER_FUNC void
_winter_array_init(winter_array_t* arr, const size_t size) {
    arr->element_size = size;
    arr->length = 0;
    arr->allocated = 8;

    arr->elements = malloc(arr->allocated * arr->element_size);
    if (arr->elements == nullptr) {
        _winter_fatal_error("Initial array allocation failed");
    }
}

WINTER_FUNC void*
_winter_array_get(const winter_array_t* arr, const size_t index) {
    return arr->elements + arr->element_size * index;
}

WINTER_FUNC void
_winter_array_push(winter_array_t* arr, const void* element) {
    if (arr->allocated <= arr->length) {
        arr->allocated *= 2;
        arr->elements = realloc(arr->elements, arr->allocated * arr->element_size);
        if (arr->elements == nullptr) {
            _winter_fatal_error("Array reallocation failed (size: %ji)", arr->allocated);
        }
    }

    memcpy(arr->elements + arr->length * arr->element_size, element, arr->element_size);
    arr->length += 1;
}

WINTER_FUNC void
_winter_initialize(void) {
    if (_winter.initialized) {
        return;
    }

    _winter.initialized = true;
    _winter_array_init(&_winter.suites, sizeof(winter_suite_t));
    _winter_array_init(&_winter.patterns, sizeof(const char*));
    _winter.print.file = stderr;
    pthread_mutex_init(&_winter.print.mutex, nullptr);
}

#define _winter_print(...) fprintf(_winter.print.file, __VA_ARGS__)

#define _winter_stringify_(a) #a
#define _winter_stringify(a) _winter_stringify_(a)

// ### MAIN FUNCTION ##################################################################################################

WINTER_FUNC double
_winter_now(void) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

WINTER_FUNC void
_winter_sleep_ms(uint32_t ms) {
    struct timespec ts = {
        .tv_sec = (time_t)(ms / 1000),
        .tv_nsec = (long)(ms % 1000) * 1000000,
    };

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        // retry with remaining time
    }
}

WINTER_FUNC void
_winter_print_timer(const double start_time) {
    double msec = _winter_now() - start_time;
    if (msec < 0) {
        msec = 0;
    }

    if (msec < 1) {
        _winter_print("(%.02fµs)", msec * 1000);
    } else if (msec < 1000) {
        _winter_print("(%.02fms)", msec);
    } else {
        _winter_print("(%.02fs)", msec / 1000);
    }
}

WINTER_FUNC void
_winter_print_unit_begin(const winter_unit_t* unit) {
    _winter_print(
      "" WINTER_COLOR_BOLD WINTER_COLOR_MAYBE "? " WINTER_COLOR_RESET WINTER_COLOR_MAYBE
      "Testing: " WINTER_COLOR_RESET WINTER_COLOR_DESC "%s" WINTER_COLOR_RESET "\n",
      unit->test->name
    );
}

WINTER_FUNC void
_winter_print_unit_debug(const winter_unit_t* unit) {
    _winter_print(
      "" WINTER_COLOR_BOLD WINTER_COLOR_FAIL "> " WINTER_COLOR_RESET WINTER_COLOR_FAIL
      "Running: " WINTER_COLOR_RESET WINTER_COLOR_DESC "%s" WINTER_COLOR_RESET "\n",
      unit->test->name
    );
}

WINTER_FUNC void
_winter_print_unit_end(const winter_unit_t* unit, const bool success) {
    if (success) {
        _winter_print(
          "" WINTER_COLOR_BOLD WINTER_COLOR_SUCCESS "✓ " WINTER_COLOR_RESET WINTER_COLOR_SUCCESS
          "Success: " WINTER_COLOR_RESET WINTER_COLOR_DESC "%s" WINTER_COLOR_RESET " ",
          unit->test->name
        );
    } else {
        _winter_print(
          "" WINTER_COLOR_BOLD WINTER_COLOR_FAIL "✕ " WINTER_COLOR_RESET WINTER_COLOR_FAIL
          "Failure: " WINTER_COLOR_RESET WINTER_COLOR_DESC "%s" WINTER_COLOR_RESET " ",
          unit->test->name
        );
    }

    _winter_print_timer(unit->start_time);
    _winter_print("\n");
}

WINTER_FUNC void
_winter_print_suite_begin(const winter_suite_t* suite) {
    _winter_print("\n" WINTER_COLOR_BOLD "Testing suite %s" WINTER_COLOR_RESET ":\n", suite->name);
}

WINTER_FUNC void
_winter_print_suite_end(const winter_suite_t* suite, const uint32_t test_count, const uint32_t success_count) {
    _winter_print(
      WINTER_COLOR_BOLD "Suite %s: Passed %i/%i tests.\n" WINTER_COLOR_RESET, suite->name, success_count, test_count
    );
}

WINTER_FUNC void
_winter_print_summary(const double start_time, const uint32_t success_count, const uint32_t test_count) {
    _winter_print(WINTER_COLOR_BOLD "\nTotal: Passed %i/%i tests. " WINTER_COLOR_RESET, success_count, test_count);
    _winter_print_timer(start_time);
    _winter_print("\n");
}

WINTER_FUNC void*
_winter_thread_entry(void* void_args) {
    const winter_args_t* args = void_args;
    _winter_local.thread_id = args->thread_id;
    args->unit->suite->func(args->unit->test->id, nullptr);
    return nullptr;
}

WINTER_FUNC void
_winter_thread_syncronize(void) {
    pthread_mutex_lock(&_winter.synchronization.mutex);

    const uint64_t generation = _winter.synchronization.generation;

    if (++_winter.synchronization.waiting == _winter.synchronization.threads) {
        _winter.synchronization.waiting = 0;
        _winter.synchronization.generation += 1;
        pthread_cond_broadcast(&_winter.synchronization.cond);
    } else {
        while (_winter.synchronization.generation == generation) {
            pthread_cond_wait(&_winter.synchronization.cond, &_winter.synchronization.mutex);
        }
    }
    pthread_mutex_unlock(&_winter.synchronization.mutex);
}

WINTER_FUNC void
_winter_process_entry(winter_unit_t* unit) {
    unit->suite->func(WINTER_FUNC_BEFORE_EACH, nullptr);

    pthread_mutex_init(&_winter.synchronization.mutex, nullptr);
    pthread_cond_init(&_winter.synchronization.cond, nullptr);
    _winter.synchronization.waiting = 0;
    _winter.synchronization.threads = unit->test->threads;
    _winter.synchronization.generation = 0;

    if (unit->test->threads == 1) {
        _winter_local.thread_id = 0;
        unit->suite->func(unit->test->id, nullptr);
    } else {
        pthread_t threads[unit->test->threads];
        winter_args_t args[unit->test->threads];

        for (uint16_t i = 0; i < unit->test->threads; ++i) {
            args[i] = (winter_args_t){
                .unit = unit,
                .thread_id = i,
            };

            if (pthread_create(&threads[i], nullptr, _winter_thread_entry, &args[i]) != 0) {
                _winter_print(WINTER_INDENT "Failed to create thread (%s).\n", strerror(errno));
                _exit(WINTER_EXIT_FAILURE);
            }
        }
        for (uint16_t i = 0; i < unit->test->threads; ++i) {
            if (pthread_join(threads[i], nullptr)) {
                _winter_print(WINTER_INDENT "Failed to join thread (%s).\n", strerror(errno));
                _exit(WINTER_EXIT_FAILURE);
            }
        }
    }

    unit->suite->func(WINTER_FUNC_AFTER_EACH, nullptr);
}

static volatile sig_atomic_t _winter_debug_abort = false;

WINTER_FUNC void
_winter_debug_handler(const int sig) {
    (void)sig;
    _winter_debug_abort = true;
}

WINTER_FUNC void
_winter_kill_process(const pid_t pid) {
    if (kill(pid, SIGCONT) == -1) {
        _winter_print(WINTER_INDENT "Failed to continue process (%s).\n", strerror(errno));
        return;
    }

    if (kill(pid, SIGKILL) == -1) {
        _winter_print(WINTER_INDENT "Failed to kill process (%s).\n", strerror(errno));
        return;
    }

    if (waitpid(pid, nullptr, 0) == -1) {
        _winter_print(WINTER_INDENT "Waiting for terminated process failed (%s).\n", strerror(errno));
    }
}

WINTER_FUNC bool
_winter_unit_debug(winter_unit_t* unit) {
    const pid_t pid = fork();
    if (pid == 0) {
        raise(SIGSTOP);
        _winter_process_entry(unit);
        _exit(0);
    }

    signal(SIGINT, _winter_debug_handler);
    _winter_debug_abort = false;

    _winter_print(WINTER_INDENT "Waiting for debugger to attach, press ctrl-c to abort... (pid %d)\n", pid);

    while (true) {
        const pid_t ret = waitpid(pid, nullptr, WNOHANG);

        if (ret == pid) {
            _winter_print(WINTER_INDENT "Test process exited. Restarting test.\n");
            break;
        }

        if (_winter_debug_abort) {
            _winter_kill_process(pid);
            _winter_print("\r" WINTER_INDENT "Waiting aborted by user.\n");
            break;
        }

        if (ret == -1 && errno != EINTR && errno != ECHILD) {
            _winter_print(WINTER_INDENT "Waiting for debug process failed (%s).\n", strerror(errno));
            break;
        }

        _winter_sleep_ms(WINTER_PROCESS_POLL_MS);
    }

    signal(SIGINT, SIG_DFL);

    return _winter_debug_abort;
}

WINTER_FUNC bool
_winter_unit_execute(winter_unit_t* unit) {
    const pid_t pid = fork();
    if (pid == 0) {
        _winter_process_entry(unit);
        _exit(0);
    }

    int status = 0;
    while (true) {
        const pid_t ret = waitpid(pid, &status, WNOHANG);

        // child process exited
        if (ret == pid) {
            break;
        }

        // no status reported by child process
        if (ret == 0) {
            if (_winter.opts.timeout && _winter_now() - unit->start_time > unit->test->timeout) {
                _winter_kill_process(pid);
                _winter_print(WINTER_INDENT "Process timed out after %.0fs.\n", (unit->test->timeout / 1000));
                return false;
            }

            _winter_sleep_ms(WINTER_PROCESS_POLL_MS);
        }

        if (ret == -1 && errno != EINTR) {
            _winter_print(WINTER_INDENT "Waiting for process failed (%s).\n", strerror(errno));
            return false;
        }
    }

    if (WIFEXITED(status)) {
        const int code = WEXITSTATUS(status);
        if (code != 0) {
            if (code != WINTER_EXIT_FAILURE) {
                _winter_print(WINTER_INDENT "Process exited with code %d.\n", code);
            }

            return false;
        }
    } else if (WIFSIGNALED(status)) {
        const int sig = WTERMSIG(status);
        _winter_print(WINTER_INDENT "Process terminated by signal %d (%s).\n", sig, strsignal(sig));
        return false;
    } else {
        _winter_print(WINTER_INDENT "Process ended abnormally (status=0x%x).\n", status);
        return false;
    }

    return true;
}

WINTER_FUNC bool
_winter_pattern_match_suite(const char* pattern, const char* name) {
    const char* separator = strchr(pattern, ':');
    const size_t length = separator ? (size_t)(separator - pattern) : strlen(pattern);

    return length == strlen(name) && strncmp(name, pattern, length) == 0;
}

WINTER_FUNC bool
_winter_pattern_match_test(const char* pattern, const char* name) {
    const char* separator = strchr(pattern, ':');
    if (separator == nullptr) {
        return true;
    }

    const int ret = fnmatch(separator + 1, name, 0);

    if (ret == 0) {
        return true;
    }
    if (ret == FNM_NOMATCH) {
        return false;
    }

    _winter_fatal_error("Failed to match pattern: %s", pattern);
}

WINTER_FUNC winter_unit_t
_winter_find_test(const char* pattern) {
    for (size_t i = 0; i < _winter.suites.length; ++i) {
        const winter_suite_t* suite = _winter_array_get(&_winter.suites, i);
        if (!_winter_pattern_match_suite(pattern, suite->name)) {
            continue;
        }

        for (size_t j = 0; j < suite->tests.length; ++j) {
            const winter_test_t* test = _winter_array_get(&suite->tests, j);
            if (!_winter_pattern_match_test(pattern, test->name)) {
                continue;
            }

            return (winter_unit_t){
                .start_time = _winter_now(),
                .suite = suite,
                .test = test,
            };
        }
    }

    _winter_fatal_error("No test found for pattern: %s", pattern);
}

WINTER_FUNC void
_winter_list(void) {
    _winter_print(WINTER_COLOR_BOLD "\nTest suites:\n" WINTER_COLOR_RESET);

    size_t total_tests = 0;
    for (size_t i = 0; i < _winter.suites.length; ++i) {
        const winter_suite_t* suite = _winter_array_get(&_winter.suites, i);
        _winter_print(WINTER_COLOR_DESC "%s:" WINTER_COLOR_RESET " %ji tests\n", suite->name, suite->tests.length);
        total_tests += suite->tests.length;
    }

    _winter_print(WINTER_COLOR_BOLD "\nTotal: %ji tests.\n" WINTER_COLOR_RESET, total_tests);
}

WINTER_FUNC void
_winter_debug(const char* pattern) {
    winter_unit_t unit = _winter_find_test(pattern);

    while (true) {
        _winter_print_unit_debug(&unit);
        if (_winter_unit_debug(&unit)) {
            break;
        }
    }
}

#define _winter_print_usage(path, u, d) fprintf(stdout, "  %s %-21s %s.\n", path, u, d);

#define _winter_print_opt_flag(n, sn, expl, d)                                                                         \
    fprintf(stdout, "  %-20s%s.\n", "--[no-]" n " | -" sn, expl);                                                      \
    fprintf(stdout, "  %-20sDefault: %s.\n", "", d)

#define _winter_print_opt_str(n, expl, d)                                                                              \
    fprintf(stdout, "  %-20s%s.\n", "--" n " value", expl);                                                            \
    fprintf(stdout, "  %-20sDefault: %s.\n", "", d)

WINTER_FUNC void
_winter_print_help(const char* path) {
    fprintf(stdout, "Usages:\n");
    _winter_print_usage(path, "[options] [patterns]", "Run all tests that match the patterns");
    _winter_print_usage(path, "--help | -h", "Print this help text and exit");
    _winter_print_usage(path, "--version | -v", "Print version and exit");
    _winter_print_usage(path, "--list | -l", "Print a list of all available tests");
    _winter_print_usage(path, "--debug pattern", "Run one test and wait for a debugger to attach to the test");

    fprintf(stdout, "\nOptions:\n");
    _winter_print_opt_flag("color", "c", "Whether to print output in color", "on when output is TTY");
    _winter_print_opt_flag("rerun", "r", "Rerun failed test and wait for a debugger to attach to the test", "off");
    _winter_print_opt_flag("pid", "p", "Print the pid of the test process", "off");
    _winter_print_opt_flag("timeout", "t", "Whether to fail a test after its timeout.", "on");
}

#define _winter_opt_flag(opt, n, sn)                                                                                   \
    opt.name = n;                                                                                                      \
    opt.short_name = sn;                                                                                               \
    opt.is_flag = true;                                                                                                \
    opt.bool_val = false;                                                                                              \
    opt.overwritten = false

#define _winter_opt_str(opt, n)                                                                                        \
    opt.name = n;                                                                                                      \
    opt.short_name = '\0';                                                                                             \
    opt.is_flag = false;                                                                                               \
    opt.str_val = nullptr;                                                                                             \
    opt.overwritten = false

#define _winter_opt_default(opt, val)                                                                                  \
    if (!opt.overwritten)                                                                                              \
    opt.bool_val = val

WINTER_FUNC void
_winter_parse_args(const int argc, const char** argv) {
    _winter_opt_t opts[_WINTER_OPT_LAST];

    _winter_opt_flag(opts[_WINTER_OPT_VERSION], "version", 'v');
    _winter_opt_flag(opts[_WINTER_OPT_HELP], "help", 'h');
    _winter_opt_flag(opts[_WINTER_OPT_LIST], "list", 'l');
    _winter_opt_flag(opts[_WINTER_OPT_COLOR], "color", 'c');
    _winter_opt_flag(opts[_WINTER_OPT_RERUN], "rerun", 'r');
    _winter_opt_flag(opts[_WINTER_OPT_TIMEOUT], "timeout", 't');

    _winter_opt_str(opts[_WINTER_OPT_DEBUG], "debug");

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (arg[0] != '-') {
            _winter_array_push(&_winter.patterns, &arg);
            continue;
        }

        const bool is_long = arg[1] == '-';
        const char* name = is_long ? arg + 2 : arg + 1;
        const bool inverted = is_long && strncmp(name, "no-", 3) == 0;

        if (inverted) {
            name += 3;
        }

        bool valid = false;
        for (uint32_t j = 0; j < _WINTER_OPT_LAST; ++j) {
            _winter_opt_t* opt = &opts[j];

            if (is_long) {
                valid = strcmp(opt->name, name) == 0;
            } else {
                valid = opt->short_name == name[0];
            }
            if (!valid) {
                continue;
            }

            opt->overwritten = true;

            if (opt->is_flag) {
                opt->bool_val = !inverted;
            } else {
                if (++i >= argc) {
                    _winter_fatal_error("Option %s requires an argument", arg);
                }

                opt->str_val = argv[i];
            }

            break;
        }

        if (!valid) {
            _winter_fatal_error("Unknown option: %s", arg);
        }
    }

    if (opts[_WINTER_OPT_HELP].bool_val) {
        _winter_print_help(argv[0]);
        _exit(EXIT_SUCCESS);
    }
    if (opts[_WINTER_OPT_VERSION].bool_val) {
        fprintf(stdout, "Winter %s\n", WINTER_VERSION);
        _exit(EXIT_SUCCESS);
    }
    if (opts[_WINTER_OPT_LIST].bool_val) {
        _winter_list();
        _exit(EXIT_SUCCESS);
    }
    if (opts[_WINTER_OPT_DEBUG].str_val != nullptr) {
        _winter_debug(opts[_WINTER_OPT_DEBUG].str_val);
        _exit(EXIT_SUCCESS);
    }

    const bool is_tty = isatty(fileno(_winter.print.file));
    const bool no_color = getenv("NO_COLOR") != nullptr;
    _winter_opt_default(opts[_WINTER_OPT_COLOR], is_tty && !no_color);
    _winter_opt_default(opts[_WINTER_OPT_TIMEOUT], true);

    _winter.opts.list = opts[_WINTER_OPT_LIST].bool_val;
    _winter.opts.color = opts[_WINTER_OPT_COLOR].bool_val;
    _winter.opts.rerun = opts[_WINTER_OPT_RERUN].bool_val;
    _winter.opts.timeout = opts[_WINTER_OPT_TIMEOUT].bool_val;
}

WINTER_FUNC bool
_winter_is_suite_enabled(const winter_suite_t* suite) {
    if (_winter.patterns.length == 0) {
        return true;
    }

    for (size_t i = 0; i < _winter.patterns.length; ++i) {
        const char** pattern = _winter_array_get(&_winter.patterns, i);
        if (_winter_pattern_match_suite(*pattern, suite->name)) {
            return true;
        }
    }

    return false;
}

WINTER_FUNC bool
_winter_is_unit_enabled(const winter_unit_t* unit) {
    if (_winter.patterns.length == 0) {
        return true;
    }

    const char* suite_name = unit->suite->name;
    const char* test_name = unit->test->name;

    for (size_t i = 0; i < _winter.patterns.length; ++i) {
        const char** pattern = _winter_array_get(&_winter.patterns, i);

        if (!_winter_pattern_match_suite(*pattern, suite_name)) {
            continue;
        }
        if (_winter_pattern_match_test(*pattern, test_name)) {
            return true;
        }
    }

    return false;
}

WINTER_FUNC int
_winter_main(const int argc, const char** argv) {
    _winter_initialize();
    _winter_parse_args(argc, argv);

    const double start_time = _winter_now();
    uint32_t global_test_count = 0;
    uint32_t global_success_count = 0;

    for (size_t i = 0; i < _winter.suites.length; ++i) {
        const winter_suite_t* suite = _winter_array_get(&_winter.suites, i);
        if (!_winter_is_suite_enabled(suite)) {
            continue;
        }

        _winter_print_suite_begin(suite);

        uint32_t suite_test_count = 0;
        uint32_t suite_success_count = 0;
        for (size_t k = 0; k < suite->tests.length; ++k) {
            winter_unit_t unit = {
                .start_time = _winter_now(),
                .suite = suite,
                .test = _winter_array_get(&suite->tests, k),
            };

            if (!_winter_is_unit_enabled(&unit)) {
                continue;
            }

            _winter_print_unit_begin(&unit);
            const bool success = _winter_unit_execute(&unit);

            while (!success && _winter.opts.rerun) {
                _winter_print_unit_debug(&unit);

                if (_winter_unit_debug(&unit)) {
                    break;
                }
            }

            _winter_print_unit_end(&unit, success);

            suite_test_count += 1;
            suite_success_count += success ? 1 : 0;
        }

        _winter_print_suite_end(suite, suite_test_count, suite_success_count);

        global_test_count += suite_test_count;
        global_success_count += suite_success_count;
    }

    _winter_print_summary(start_time, global_success_count, global_test_count);

    if (global_test_count == global_success_count) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}

#ifdef WINTER_TEST

#define winter_main()                                                                                                  \
    winter_t _winter;                                                                                                  \
    _Thread_local winter_local_t _winter_local;                                                                        \
                                                                                                                       \
    int main(const int argc, const char** argv) {                                                                      \
        return _winter_main(argc, argv);                                                                               \
    }                                                                                                                  \
                                                                                                                       \
    __attribute__((unused)) static int _winter_main_unused_variable_for_semicolon

#else

#define winter_main() __attribute__((unused)) static int _winter_main_unused_variable_for_semicolon

#endif

// ### ASSERTIONS #####################################################################################################

#define _winter_fail_update()                                                                                          \
    do {                                                                                                               \
        _winter_local.filename = __FILE__;                                                                             \
        _winter_local.linenum = __LINE__;                                                                              \
    } while (0)

#define _winter_fail_begin()                                                                                           \
    do {                                                                                                               \
        pthread_mutex_lock(&_winter.print.mutex);                                                                      \
    } while (0)

#define _winter_fail_end()                                                                                             \
    do {                                                                                                               \
        _winter_print("    in %s:%i\n", _winter_local.filename, _winter_local.linenum);                                \
        _exit(WINTER_EXIT_FAILURE);                                                                                    \
    } while (0)

#define _winter_fail(fmt, ...)                                                                                         \
    do {                                                                                                               \
        _winter_fail_begin();                                                                                          \
        _winter_print(WINTER_INDENT fmt ".\n", __VA_ARGS__);                                                           \
        _winter_fail_end();                                                                                            \
    } while (0)

#define _winter_fail_expl(expl, fmt, ...)                                                                              \
    do {                                                                                                               \
        if (expl[0] == '\0') {                                                                                         \
            _winter_fail(fmt, __VA_ARGS__);                                                                            \
        } else {                                                                                                       \
            _winter_fail(fmt ": %s", __VA_ARGS__, expl);                                                               \
        }                                                                                                              \
    } while (0)

#define _winter_assertfunc(name, type, pattern)                                                                        \
    WINTER_FUNC void _winter_assert_##name(                                                                            \
      const bool invert, const char* explanation, const type a, const char* a_str, const type b, const char* b_str     \
    ) {                                                                                                                \
        const bool eq = (a) == (b);                                                                                    \
        if (!eq && !invert) {                                                                                          \
            _winter_fail_expl(                                                                                         \
              explanation, "(" #name ") Expected %s to equal %s (" pattern "), but got " pattern, a_str, b_str, b, a   \
            );                                                                                                         \
        }                                                                                                              \
        if (eq && invert) {                                                                                            \
            _winter_fail_expl(explanation, "(" #name ") Expected %s to not equal %s (" pattern ")", a_str, b_str, a);  \
        }                                                                                                              \
    }                                                                                                                  \
    __attribute__((unused)) static int _assertfunc_unused_variable_for_semicolon

_winter_assertfunc(int, intmax_t, "%ji");

_winter_assertfunc(uint, uintmax_t, "%ju");

_winter_assertfunc(ptr, void*, "%p");

WINTER_FUNC void
_winter_assert_str(
  const bool invert,
  const char* explanation,
  const char* a,
  const char* a_str,
  const char* b,
  const char* b_str
) {
    const bool eq = strcmp(a, b) == 0;
    if (!eq && !invert) {
        _winter_fail_expl(explanation, "(str) Expected %s to equal %s (\"%s\"), but got \"%s\"", a_str, b_str, b, a);
    }
    if (eq && invert) {
        _winter_fail_expl(explanation, "(str) Expected %s to not equal %s (\"%s\")", a_str, b_str, a);
    }
}

#define fail(fmt, ...)                                                                                                 \
    do {                                                                                                               \
        _winter_fail_update();                                                                                         \
        _winter_fail(fmt, __VA_ARGS__);                                                                                \
    } while (0)

#define assertis(x, ...)                                                                                               \
    do {                                                                                                               \
        _winter_fail_update();                                                                                         \
        if (!(x)) {                                                                                                    \
            _winter_fail_expl("" __VA_ARGS__, "Assertion failed: %s", #x);                                             \
        }                                                                                                              \
    } while (0)

#define asserteq_ptr(a, b, ...)                                                                                        \
    do {                                                                                                               \
        _winter_fail_update();                                                                                         \
        _winter_assert_ptr(false, "" __VA_ARGS__, (a), #a, (b), #b);                                                   \
    } while (0)

#define asserteq_int(a, b, ...)                                                                                        \
    do {                                                                                                               \
        _winter_fail_update();                                                                                         \
        _winter_assert_int(false, "" __VA_ARGS__, (a), #a, (b), #b);                                                   \
    } while (0)

#define asserteq_uint(a, b, ...)                                                                                       \
    do {                                                                                                               \
        _winter_fail_update();                                                                                         \
        _winter_assert_uint(false, "" __VA_ARGS__, (a), #a, (b), #b);                                                  \
    } while (0)

#define asserteq_str(a, b, ...)                                                                                        \
    do {                                                                                                               \
        _winter_fail_update();                                                                                         \
        _winter_assert_str(false, "" __VA_ARGS__, (a), #a, (b), #b);                                                   \
    } while (0)

#define assertneq_ptr(a, b, ...)                                                                                       \
    do {                                                                                                               \
        _winter_fail_update();                                                                                         \
        _winter_assert_ptr(true, "" __VA_ARGS__, (a), #a, (b), #b);                                                    \
    } while (0)

#define assertneq_int(a, b, ...)                                                                                       \
    do {                                                                                                               \
        _winter_fail_update();                                                                                         \
        _winter_assert_int(true, "" __VA_ARGS__, (a), #a, (b), #b);                                                    \
    } while (0)

#define assertneq_uint(a, b, ...)                                                                                      \
    do {                                                                                                               \
        _winter_fail_update();                                                                                         \
        _winter_assert_uint(true, "" __VA_ARGS__, (a), #a, (b), #b);                                                   \
    } while (0)

#define assertneq_str(a, b, ...)                                                                                       \
    do {                                                                                                               \
        _winter_fail_update();                                                                                         \
        _winter_assert_str(true, "" __VA_ARGS__, (a), #a, (b), #b);                                                    \
    } while (0)

#define assert_success(expr, ...)                                                                                      \
    do {                                                                                                               \
        if ((expr) == SUCCESS) {                                                                                       \
            break;                                                                                                     \
        }                                                                                                              \
                                                                                                                       \
        _winter_fail_update();                                                                                         \
        _winter_fail_begin();                                                                                          \
                                                                                                                       \
        _winter_print(WINTER_INDENT "(result) Expected success of %s, but got %d", #expr, error_get_code());           \
                                                                                                                       \
        __VA_OPT__(_winter_print(" : %s", "" __VA_ARGS__);)                                                            \
        _winter_print(".\n");                                                                                          \
                                                                                                                       \
        for (uint32_t _i = 0, _n = error_trace_length(); _i < _n; ++_i) {                                              \
            const error_frame_t* _frame = error_trace_nth(_i);                                                         \
            _winter_print(                                                                                             \
              WINTER_INDENT "at %s:%d in %s: %s\n", _frame->file, _frame->line, _frame->func, _frame->msg              \
            );                                                                                                         \
        }                                                                                                              \
                                                                                                                       \
        _winter_fail_end();                                                                                            \
    } while (0)

#define assert_failure(expr, code, ...)                                                                                \
    do {                                                                                                               \
        _winter_fail_update();                                                                                         \
                                                                                                                       \
        if ((expr) != FAILURE) {                                                                                       \
            _winter_fail_expl("" __VA_ARGS__, "(result) Expected failure of %s", #expr);                               \
        }                                                                                                              \
        if (error_get_code() != code) {                                                                                \
            _winter_fail_expl(                                                                                         \
              "" __VA_ARGS__,                                                                                          \
              "(result) Expected error code of %s to be equal to %d, but got %d",                                      \
              #expr,                                                                                                   \
              code,                                                                                                    \
              error_get_code()                                                                                         \
            );                                                                                                         \
        }                                                                                                              \
    } while (0)

// ### TEST CREATION ###################################################################################################

#ifdef WINTER_TEST

#define describe(name)                                                                                                 \
    static void _winter_test_##name(uint64_t, winter_array_t*);                                                        \
                                                                                                                       \
    __attribute__((constructor(__COUNTER__ + 203))) static void _winter_constructor_pager(void) {                      \
        _winter_initialize();                                                                                          \
                                                                                                                       \
        winter_array_t tests;                                                                                          \
        _winter_array_init(&tests, sizeof(winter_test_t));                                                             \
                                                                                                                       \
        _winter_test_##name(WINTER_FUNC_INFO, &tests);                                                                 \
                                                                                                                       \
        const winter_suite_t suite = { #name, tests, _winter_test_##name };                                            \
        _winter_array_push(&_winter.suites, &suite);                                                                   \
    }                                                                                                                  \
                                                                                                                       \
    __attribute__((optnone)) static void _winter_test_##name(                                                          \
      const uint64_t index __attribute__((unused)), winter_array_t* out __attribute__((unused))                        \
    )

#define TEST_ONLY

#else

#define describe(name)                                                                                                 \
    WINTER_FUNC void _winter_unused_##name(                                                                            \
      const uint64_t index __attribute__((unused)), winter_array_t* out __attribute__((unused))                        \
    )

#define TEST_ONLY __attribute__((unused, deprecated("should not be used outside of tests")))

#endif

#define _winter_test(name, i, threads, timeout)                                                                        \
    if (index == WINTER_FUNC_INFO) {                                                                                   \
        _winter_array_push(out, &(winter_test_t){ name, i + 6, threads, timeout });                                    \
    }                                                                                                                  \
    if (index == i + 6)

#define test(name, timeout, threads) _winter_test(name, __COUNTER__, threads, timeout * 1000)

#define it(name) _winter_test(name, __COUNTER__, 1, WINTER_DEFAULT_TIMEOUT_MS)

#define parallel(name, threads)                                                                                        \
    _winter_test(name " (parallel " #threads ")", __COUNTER__, threads, WINTER_DEFAULT_TIMEOUT_MS)

#define before_each() if (index == WINTER_FUNC_BEFORE_EACH)

#define after_each() if (index == WINTER_FUNC_AFTER_EACH)

#define thread_index() _winter_local.thread_id

#define synchronize() _winter_thread_syncronize()
