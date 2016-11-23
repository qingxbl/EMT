/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTMULTIPOOL_H__
#define __EMTMULTIPOOL_H__

#include "EMTPool.h"

#define SIZEOF_EMTMULTIPOOL(configs) (sizeof(EMTMULTIPOOLCONFIG) + sizeof(EMTMULTIPOOL) * (configs))

typedef struct _EMTMULTIPOOLCONFIG EMTMULTIPOOLCONFIG, *PEMTMULTIPOOLCONFIG;
typedef struct _EMTMULTIPOOL EMTMULTIPOOL, *PEMTMULTIPOOL;

struct _EMTMULTIPOOLCONFIG
{
	uint32_t uBlockLength;
	uint32_t uBlockCount;
	uint32_t uBlockLimit;

	EMTPOOL sPool;

	uint8_t reserved[4];
};

struct _EMTMULTIPOOL
{
	const void (*init)(PEMTMULTIPOOL pThis, void * pMeta, void * pPool);

	const uint32_t (*id)(PEMTMULTIPOOL pThis);
	const PEMTPOOL (*pool)(PEMTMULTIPOOL pThis, const uint32_t uPool);

	const PEMTPOOL (*poolByMem)(PEMTMULTIPOOL pThis, void * pMem);
	const uint32_t (*length)(PEMTMULTIPOOL pThis, void * pMem);

	void * (*alloc)(PEMTMULTIPOOL pThis, const uint32_t uMemLen);
	void (*free)(PEMTMULTIPOOL pThis, void * pMem);
	void (*freeAll)(PEMTMULTIPOOL pThis, const uint32_t uId);

	const uint32_t (*transfer)(PEMTMULTIPOOL pThis, void * pMem, const uint32_t uToId);
	void * (*take)(PEMTMULTIPOOL pThis, const uint32_t uToken);

	uint8_t reserved[32];

	uint32_t uPoolCount;
};

EXTERN_C uint32_t constructEMTMultiPool(PEMTMULTIPOOL pEMTPool, uint32_t * pMemLen);
EXTERN_C void destructEMTMultiPool(PEMTMULTIPOOL pEMTPool);

#endif // __EMTMULTIPOOL_H__
