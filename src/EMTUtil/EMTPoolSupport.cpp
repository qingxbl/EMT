#include "stable.h"

#include "EMTPoolSupport.h"

#include <EMTUtil/EMTPool.h>

EXTERN_C void * rt_memset(void *mem, const int val, const uint32_t size) { return memset(mem, val, size); }
EXTERN_C uint32_t rt_cmpXchg32(volatile uint32_t *dest, uint32_t exchg, uint32_t comp) { return (uint32_t)::InterlockedCompareExchange((volatile LONG *)dest, (LONG)exchg, (LONG)comp); }
