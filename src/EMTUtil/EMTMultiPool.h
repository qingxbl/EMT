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

#if !defined(USE_VTABLE) || defined(EMTIMPL_MULTIPOOL)
EMTIMPL_CALL void EMTMultiPool_calcMetaSize(PEMTMULTIPOOL pThis, uint32_t * pMetaLen, uint32_t * pMemLen);
EMTIMPL_CALL void EMTMultiPool_construct(PEMTMULTIPOOL pThis, void * pMeta, void * pPool);
EMTIMPL_CALL void EMTMultiPool_destruct(PEMTMULTIPOOL pThis);
EMTIMPL_CALL const uint32_t EMTMultiPool_id(PEMTMULTIPOOL pThis);
EMTIMPL_CALL const PEMTPOOL EMTMultiPool_pool(PEMTMULTIPOOL pThis, const uint32_t uPool);
EMTIMPL_CALL const PEMTPOOL EMTMultiPool_poolByMem(PEMTMULTIPOOL pThis, void * pMem);
EMTIMPL_CALL const uint32_t EMTMultiPool_length(PEMTMULTIPOOL pThis, void * pMem);
EMTIMPL_CALL void * EMTMultiPool_alloc(PEMTMULTIPOOL pThis, const uint32_t uMemLen);
EMTIMPL_CALL void EMTMultiPool_free(PEMTMULTIPOOL pThis, void * pMem);
EMTIMPL_CALL void EMTMultiPool_freeAll(PEMTMULTIPOOL pThis, const uint32_t uId);
EMTIMPL_CALL const uint32_t EMTMultiPool_transfer(PEMTMULTIPOOL pThis, void * pMem, const uint32_t uToId);
EMTIMPL_CALL void * EMTMultiPool_take(PEMTMULTIPOOL pThis, const uint32_t uToken);
#else
#define EMTMultiPool_calcMetaSize emtMultiPool()->calcMetaSize
#define EMTMultiPool_construct emtMultiPool()->construct
#define EMTMultiPool_destruct emtMultiPool()->destruct
#define EMTMultiPool_id emtMultiPool()->id
#define EMTMultiPool_pool emtMultiPool()->pool
#define EMTMultiPool_poolByMem emtMultiPool()->poolByMem
#define EMTMultiPool_length emtMultiPool()->length
#define EMTMultiPool_alloc emtMultiPool()->alloc
#define EMTMultiPool_free emtMultiPool()->free
#define EMTMultiPool_freeAll emtMultiPool()->freeAll
#define EMTMultiPool_transfer emtMultiPool()->transfer
#define EMTMultiPool_take emtMultiPool()->take
#endif

#endif // __EMTMULTIPOOL_H__
