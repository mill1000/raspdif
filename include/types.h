#ifndef __TYPES__
#define __TYPES__

#if INTPTR_MAX == INT64_MAX
// 64-bit
typedef uint32_t uintptr32_t;
#define NULL_PTR32 0

#elif INTPTR_MAX == INT32_MAX
// 32-bit
typedef uintptr_t uintptr32_t;
#define NULL_PTR32 NULL

#else
#error Unknown pointer size
#endif

#endif
