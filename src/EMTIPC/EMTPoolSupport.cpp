#include "stable.h"

#include <EMTPool/EMTPool.h>

#include <Windows.h>

EXTERN_C void * rt_memset(void *mem, const int val, const uint32_t size) { return memset(mem, val, size); }
EXTERN_C uint16_t rt_cmpXchg16(volatile uint16_t *dest, uint16_t exchg, uint16_t comp) { return (uint16_t)InterlockedCompareExchange16((volatile SHORT *)dest, (SHORT)exchg, (SHORT)comp); }
EXTERN_C uint32_t rt_cmpXchg32(volatile uint32_t *dest, uint32_t exchg, uint32_t comp) { return (uint32_t)InterlockedCompareExchange((volatile LONG *)dest, (LONG)exchg, (LONG)comp); }
