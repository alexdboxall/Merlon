#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#define export __attribute__((used))

#ifndef NULL
#define NULL ((void*) 0)
#endif

#define warn_unused __attribute__((warn_unused_result))
#define always_inline __attribute__((always_inline)) inline

#define PAGEABLE_CODE_SECTION __attribute__((__section__(".pageablektext")))
#define PAGEABLE_DATA_SECTION __attribute__((__section__(".pageablekdata")))

#define NO_EXPORT __attribute__((visibility("hidden")))
#define EXPORT __attribute__((visibility("default")))

#define LOCKED_DRIVER_CODE __attribute__((__section__(".lockedtext")))
#define LOCKED_DRIVER_DATA __attribute__((__section__(".lockeddata")))
#define LOCKED_DRIVER_RODATA __attribute__((__section__(".lockedrodata")))

#define inline_memcpy(dst, src, n) __builtin_memcpy(dst, src, n)
#define inline_memset(dst, v, n) __builtin_memset(dst, v, n)