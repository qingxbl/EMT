/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTPOOL_H__
#define __EMTPOOL_H__

#include <EMTCommon.h>

typedef struct _EMTPOOLOPS EMTPOOLOPS, * PEMTPOOLOPS;
typedef const EMTPOOLOPS * PCEMTPOOLOPS;
typedef struct _EMTPOOL EMTPOOL, * PEMTPOOL;
typedef struct _EMTPOOLBLOCKMETA EMTPOOLBLOCKMETA, *PEMTPOOLBLOCKMETA;
typedef struct _EMTPOOLMETA EMTPOOLMETA, *PEMTPOOLMETA;

struct _EMTPOOLOPS
{
	void (*calcMetaSize)(const uint32_t uBlockCount, const uint32_t uBlockLen, uint32_t * pMetaLen, uint32_t * pMemLen);

	void (*construct)(PEMTPOOL pThis, const uint32_t uId, const uint32_t uBlockCount, const uint32_t uBlockLen, void * pMeta, void * pPool);
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

EXTERN_C void * rt_memset(void *mem, const int val, const uint32_t size);
EXTERN_C uint32_t rt_cmpXchg32(volatile uint32_t *dest, uint32_t exchg, uint32_t comp);

#endif // __EMTPOOL_H__
