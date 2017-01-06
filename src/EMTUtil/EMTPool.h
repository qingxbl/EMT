/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTPOOL_H__
#define __EMTPOOL_H__

#include "EMTCommon.h"

typedef struct _EMTPOOLOPS EMTPOOLOPS, * PEMTPOOLOPS;
typedef const EMTPOOLOPS * PCEMTPOOLOPS;
typedef struct _EMTPOOL EMTPOOL, * PEMTPOOL;
typedef struct _EMTPOOLBLOCKMETA EMTPOOLBLOCKMETA, *PEMTPOOLBLOCKMETA;
typedef struct _EMTPOOLMETA EMTPOOLMETA, *PEMTPOOLMETA;

struct _EMTPOOLOPS
{
	void (*calcMetaSize)(const uint32_t uBlockCount, const uint32_t uBlockLen, uint32_t * pMetaLen, uint32_t * pMemLen);

	void (*construct)(PEMTPOOL pThis, const uint32_t uId, const uint32_t uBlockCount, const uint32_t uBlockLen, const uint32_t uBlockInit, void * pMeta, void * pPool);
	void (*destruct)(PEMTPOOL pThis);

	const uint32_t (*id)(PEMTPOOL pThis);
	void * (*address)(PEMTPOOL pThis);
	const uint32_t (*poolLength)(PEMTPOOL pThis);
	const uint32_t (*blockLength)(PEMTPOOL pThis);
	const uint32_t (*blockCount)(PEMTPOOL pThis);

	const uint32_t (*length)(PEMTPOOL pThis, void * pMem);

	void * (*alloc)(PEMTPOOL pThis, const uint32_t uMemLen);
	void (*free)(PEMTPOOL pThis, void * pMem);
	void (*freeAll)(PEMTPOOL pThis, const uint32_t uId);

	const uint32_t (*transfer)(PEMTPOOL pThis, void * pMem, const uint32_t uToId);
	void * (*take)(PEMTPOOL pThis, const uint32_t uToken);
};

struct _EMTPOOL
{
	/* Private fields */
	void * pPool;

	PEMTPOOLMETA pMeta;
	PEMTPOOLBLOCKMETA pBlockMeta;

	uint32_t uId;
};

EXTERN_C PCEMTPOOLOPS emtPool(void);

#if !defined(USE_VTABLE) || defined(EMTIMPL_POOL)
EMTIMPL_CALL void EMTPool_calcMetaSize(const uint32_t uBlockCount, const uint32_t uBlockLen, uint32_t * pMetaLen, uint32_t * pMemLen);
EMTIMPL_CALL void EMTPool_construct(PEMTPOOL pThis, const uint32_t uId, const uint32_t uBlockCount, const uint32_t uBlockLen, const uint32_t uBlockInit, void * pMeta, void * pPool);
EMTIMPL_CALL void EMTPool_destruct(PEMTPOOL pThis);
EMTIMPL_CALL const uint32_t EMTPool_id(PEMTPOOL pThis);
EMTIMPL_CALL void * EMTPool_address(PEMTPOOL pThis);
EMTIMPL_CALL const uint32_t EMTPool_poolLength(PEMTPOOL pThis);
EMTIMPL_CALL const uint32_t EMTPool_blockLength(PEMTPOOL pThis);
EMTIMPL_CALL const uint32_t EMTPool_blockCount(PEMTPOOL pThis);
EMTIMPL_CALL const uint32_t EMTPool_length(PEMTPOOL pThis, void * pMem);
EMTIMPL_CALL void * EMTPool_alloc(PEMTPOOL pThis, const uint32_t uMemLen);
EMTIMPL_CALL void EMTPool_free(PEMTPOOL pThis, void * pMem);
EMTIMPL_CALL void EMTPool_freeAll(PEMTPOOL pThis, const uint32_t uId);
EMTIMPL_CALL const uint32_t EMTPool_transfer(PEMTPOOL pThis, void * pMem, const uint32_t uToId);
EMTIMPL_CALL void * EMTPool_take(PEMTPOOL pThis, const uint32_t uToken);
#else
#define EMTPool_calcMetaSize emtPool()->calcMetaSize
#define EMTPool_construct emtPool()->construct
#define EMTPool_destruct emtPool()->destruct
#define EMTPool_id emtPool()->id
#define EMTPool_address emtPool()->address
#define EMTPool_poolLength emtPool()->poolLength
#define EMTPool_blockLength emtPool()->blockLength
#define EMTPool_blockCount emtPool()->blockCount
#define EMTPool_length emtPool()->length
#define EMTPool_alloc emtPool()->alloc
#define EMTPool_free emtPool()->free
#define EMTPool_freeAll emtPool()->freeAll
#define EMTPool_transfer emtPool()->transfer
#define EMTPool_take emtPool()->take
#endif

EXTERN_C void * rt_memset(void * mem, const int val, const uint32_t size);
EXTERN_C uint32_t rt_cmpXchg32(volatile uint32_t * dest, uint32_t exchg, uint32_t comp);

#endif // __EMTPOOL_H__
