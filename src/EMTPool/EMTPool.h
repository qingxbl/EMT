#ifndef __EMTPOOL_H__
#define __EMTPOOL_H__

#include <stdint.h>

/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif // __cplusplus
#endif // EXTERN_C

typedef struct _EMTPOOL EMTPOOL, *PEMTPOOL;
struct _EMTPOOL
{
	const uint32_t (*id)(PEMTPOOL pThis);

	void * (*alloc)(PEMTPOOL pThis, const uint32_t uMemLen);
	void (*free)(PEMTPOOL pThis, void * pMem);
	void (*freeAll)(PEMTPOOL pThis, const uint32_t uId);

	const uint32_t (*transfer)(PEMTPOOL pThis, void * pMem, const uint32_t uToId);
	const void * (*take)(PEMTPOOL pThis, const uint32_t uToken);

	uint8_t reserved[32];
};

EXTERN_C void constructEMTPool(PEMTPOOL pEMTPool, void * pMem, const uint32_t uMemLen, const uint32_t uBlockLen);
EXTERN_C void destructEMTPool(PEMTPOOL pEMTPool);

EXTERN_C void * rt_memset(void *mem, const int val, const uint32_t size);
EXTERN_C uint16_t rt_cmpXchg16(volatile uint16_t *dest, uint16_t exchg, uint16_t comp);
EXTERN_C uint32_t rt_cmpXchg32(volatile uint32_t *dest, uint32_t exchg, uint32_t comp);

#endif //__EMTPOOL_H__
