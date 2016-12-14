/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTMULTIPOOL_H__
#define __EMTMULTIPOOL_H__

#include "EMTPool.h"

typedef struct _EMTMULTIPOOLOPS EMTMULTIPOOLOPS, * PEMTMULTIPOOLOPS;
typedef const EMTMULTIPOOLOPS * PCEMTMULTIPOOLOPS;
typedef struct _EMTMULTIPOOLCONFIG EMTMULTIPOOLCONFIG, * PEMTMULTIPOOLCONFIG;
typedef struct _EMTMULTIPOOL EMTMULTIPOOL, * PEMTMULTIPOOL;
typedef struct _EMTMULTIPOOLMETA EMTMULTIPOOLMETA, *PEMTMULTIPOOLMETA;

struct _EMTMULTIPOOLOPS
{
	void (*calcMetaSize)(PEMTMULTIPOOL pThis, uint32_t * pMetaLen, uint32_t * pMemLen);

	void (*construct)(PEMTMULTIPOOL pThis, void * pMeta, void * pPool);
	void (*destruct)(PEMTMULTIPOOL pThis);

	const uint32_t (*id)(PEMTMULTIPOOL pThis);
	const PEMTPOOL (*pool)(PEMTMULTIPOOL pThis, const uint32_t uPool);

	const PEMTPOOL (*poolByMem)(PEMTMULTIPOOL pThis, void * pMem);
	const uint32_t (*length)(PEMTMULTIPOOL pThis, void * pMem);

	void * (*alloc)(PEMTMULTIPOOL pThis, const uint32_t uMemLen);
	void (*free)(PEMTMULTIPOOL pThis, void * pMem);
	void (*freeAll)(PEMTMULTIPOOL pThis, const uint32_t uId);

	const uint32_t (*transfer)(PEMTMULTIPOOL pThis, void * pMem, const uint32_t uToId);
	void * (*take)(PEMTMULTIPOOL pThis, const uint32_t uToken);
};

struct _EMTMULTIPOOLCONFIG
{
	/* Public fields - init */
	uint32_t uBlockLength;
	uint32_t uBlockCount;
	uint32_t uBlockLimit;

	/* Private fields */
	uint32_t uBlockLimitLength;

	EMTPOOL sPool;
};

struct _EMTMULTIPOOL
{
	/* Private fields */
	uint32_t uId;

	uint32_t uBlockLimitLength;

	PEMTMULTIPOOLMETA pMeta;
	uint8_t * pMemMap;

	void * pMem;
	void * pMemEnd;

	/* Public fields - init */
	uint32_t uPoolCount;
};

EXTERN_C PCEMTMULTIPOOLOPS emtMultiPool(void);

#endif // __EMTMULTIPOOL_H__
