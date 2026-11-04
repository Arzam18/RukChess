// stdafx.h
// Platform includes and compatibility helpers.

#pragma once
#ifndef STDAFX_H
#define STDAFX_H

#if defined(_WIN32)
#include "targetver.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timeb.h>
#include <process.h>
#include <omp.h>
#include <intrin.h>
#include <immintrin.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <assert.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

// The mobile build deliberately runs one search thread. OpenMP pragmas in the
// upstream source remain intact and are ignored by the Android build.
static inline void omp_set_dynamic(int) {}
static inline int omp_get_max_threads(void) { return 1; }
static inline int omp_get_num_threads(void) { return 1; }
static inline int omp_get_thread_num(void) { return 0; }
static inline void omp_set_num_threads(int) {}

#define _declspec(x) __attribute__((aligned(64)))
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#define _MM_HINT_T0 3
#define _mm_prefetch(p, hint) __builtin_prefetch((p), 0, 3)

static inline int fopen_s(FILE** outFile, const char* filename, const char* mode)
{
    if (outFile == NULL) return EINVAL;
    *outFile = fopen(filename, mode);
    return *outFile != NULL ? 0 : (errno != 0 ? errno : 1);
}

static inline int strcpy_s(char* destination, size_t destinationSize, const char* source)
{
    if (destination == NULL || source == NULL || destinationSize == 0) return EINVAL;
    const size_t length = strlen(source);
    if (length >= destinationSize) {
        destination[0] = '\0';
        return ERANGE;
    }
    memcpy(destination, source, length + 1);
    return 0;
}

static inline int sprintf_s(char* destination, size_t destinationSize, const char* format, ...)
{
    if (destination == NULL || format == NULL || destinationSize == 0) return -1;
    va_list args;
    va_start(args, format);
    const int result = vsnprintf(destination, destinationSize, format, args);
    va_end(args);
    if (result < 0 || (size_t)result >= destinationSize) {
        destination[0] = '\0';
        return -1;
    }
    return result;
}

static inline int scanf_s(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = vscanf(format, args);
    va_end(args);
    return result;
}

typedef struct { long long QuadPart; } LARGE_INTEGER;
static inline int QueryPerformanceCounter(LARGE_INTEGER* value)
{
    if (value == NULL) return 0;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    value->QuadPart = (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
    return 1;
}

static inline int QueryPerformanceFrequency(LARGE_INTEGER* value)
{
    if (value == NULL) return 0;
    value->QuadPart = 1000000000LL;
    return 1;
}

static inline void Sleep(unsigned long milliseconds)
{
    struct timespec request;
    request.tv_sec = (time_t)(milliseconds / 1000UL);
    request.tv_nsec = (long)(milliseconds % 1000UL) * 1000000L;
    while (nanosleep(&request, &request) == -1 && errno == EINTR) {}
}

struct RukChessThreadStart
{
    void (*function)(void*);
    void* argument;
};

static inline void* rukchess_thread_trampoline(void* opaque)
{
    RukChessThreadStart* start = (RukChessThreadStart*)opaque;
    void (*function)(void*) = start->function;
    void* argument = start->argument;
    free(start);
    function(argument);
    return NULL;
}

static inline void _endthread(void) {}
static inline uintptr_t _beginthread(void (*function)(void*), unsigned int, void* argument)
{
    if (function == NULL) return (uintptr_t)-1;
    RukChessThreadStart* start = (RukChessThreadStart*)malloc(sizeof(RukChessThreadStart));
    if (start == NULL) return (uintptr_t)-1;
    start->function = function;
    start->argument = argument;

    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) {
        free(start);
        return (uintptr_t)-1;
    }
    // Search uses recursive frames; give the detached worker a 2 MiB stack.
    pthread_attr_setstacksize(&attributes, 2U * 1024U * 1024U);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);

    pthread_t thread;
    const int result = pthread_create(&thread, &attributes, rukchess_thread_trampoline, start);
    pthread_attr_destroy(&attributes);
    if (result != 0) {
        free(start);
        return (uintptr_t)-1;
    }
    return (uintptr_t)1;
}
#endif // _WIN32
#endif // !STDAFX_H
