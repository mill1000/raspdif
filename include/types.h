#ifndef __TYPES__
#define __TYPES__

#include <stddef.h>
#include <stdint.h>

#if INTPTR_MAX == INT64_MAX
// 64-bit
#define TARGET_64BIT

typedef uint32_t uintptr32_t;
#define PTR32_NULL    ((uintptr32_t)0)
#define PTR32_CAST(A) (uint32_t)(uintptr_t)(A)

#elif INTPTR_MAX == INT32_MAX
// 32-bit
#define TARGET_32BIT

typedef uintptr_t uintptr32_t;
#define PTR32_NULL    ((uintptr32_t)NULL)
#define PTR32_CAST(A) (uintptr_t)(A)

#else
#error Unknown pointer size
#endif

#endif
