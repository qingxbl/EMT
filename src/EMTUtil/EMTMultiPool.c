#include "EMTMultiPool.h"

enum
{
	kEMTMultiPoolPageShift = 12,
	kEMTMultiPoolPoolIdShift = 24,
	kEMTMultiPoolTokenMask = (1 << 24) - 1,

	kEMTMultiPoolInvalidToken = ~0x0,
};

static uint32_t EMTMultiPool_construct(PEMTMULTIPOOL pThis, uint32_t * pMemLen);
static void EMTMultiPool_destruct(PEMTMULTIPOOL pThis);
static void EMTMultiPool_init(PEMTMULTIPOOL pThis, void * pMeta, void * pPool);
static const uint32_t EMTMultiPool_id(PEMTMULTIPOOL pThis);
const PEMTPOOL EMTMultiPool_pool(PEMTMULTIPOOL pThis, const uint32_t uPool);
const PEMTPOOL EMTMultiPool_poolByMem(PEMTMULTIPOOL pThis, void * pMem);
const uint32_t EMTMultiPool_length(PEMTMULTIPOOL pThis, void * pMem);
static void * EMTMultiPool_alloc(PEMTMULTIPOOL pThis, const uint32_t uMemLen);
static void EMTMultiPool_free(PEMTMULTIPOOL pThis, void * pMem);
static void EMTMultiPool_freeAll(PEMTMULTIPOOL pThis, const uint32_t uId);
static const uint32_t EMTMultiPool_transfer(PEMTMULTIPOOL pThis, void * pMem, const uint32_t uToId);
static void * EMTMultiPool_take(PEMTMULTIPOOL pThis, const uint32_t uToken);

typedef struct _EMTMULTIPOOLPRIVATE EMTMULTIPOOLPRIVATE, *PEMTMULTIPOOLPRIVATE;
typedef struct _EMTMULTIPOOLCONFIGPRIVATE EMTMULTIPOOLCONFIGPRIVATE, *PEMTMULTIPOOLCONFIGPRIVATE;
typedef struct _EMTMULTIPOOLMETA EMTMULTIPOOLMETA, *PEMTMULTIPOOLMETA;

struct _EMTMULTIPOOLPRIVATE
{
	uint32_t uId;

	uint32_t uBlockLimitLength;

	PEMTMULTIPOOLMETA pMeta;
	uint8_t * pMemMap;

	void * pMem;
	void * pMemEnd;
};

struct _EMTMULTIPOOLCONFIGPRIVATE
{
	uint32_t uBlockLimitLength;
};

struct _EMTMULTIPOOLMETA
{
	volatile uint32_t uNextId;
};

#define EMT_PRIVATE(TYPE, NAME, VALUE) TYPE##PRIVATE * NAME = (TYPE##PRIVATE *)VALUE->reserved
#define EMT_D(TYPE) EMT_PRIVATE(TYPE, d, pThis)

static PEMTMULTIPOOLCONFIG EMTMultiPool_poolConfig(PEMTMULTIPOOL pThis, const uint32_t uPool)
{
	EMT_D(EMTMULTIPOOL);
	return uPool < pThis->uPoolCount ? (PEMTMULTIPOOLCONFIG)(pThis + 1) + uPool : 0;
}

static PEMTMULTIPOOLCONFIG EMTMultiPool_poolConfigByMem(PEMTMULTIPOOL pThis, void * pMem)
{
	EMT_D(EMTMULTIPOOL);

	if (pMem < d->pMem || pMem > d->pMemEnd)
		return 0;

	return EMTMultiPool_poolConfig(pThis, d->pMemMap[((uint8_t *)pMem - (uint8_t *)d->pMem) >> kEMTMultiPoolPageShift]);
}

static uint32_t EMTMultiPool_construct(PEMTMULTIPOOL pThis, uint32_t * pMemLen)
{
	EMT_D(EMTMULTIPOOL);
	uint32_t metaLength = sizeof(*d->pMeta);
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	pThis->init = EMTMultiPool_init;
	pThis->id = EMTMultiPool_id;
	pThis->pool = EMTMultiPool_pool;
	pThis->poolByMem = EMTMultiPool_poolByMem;
	pThis->length = EMTMultiPool_length;
	pThis->alloc = EMTMultiPool_alloc;
	pThis->free = EMTMultiPool_free;
	pThis->freeAll = EMTMultiPool_freeAll;
	pThis->transfer = EMTMultiPool_transfer;
	pThis->take = EMTMultiPool_take;

	*pMemLen = 0;
	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		EMT_PRIVATE(EMTMULTIPOOLCONFIG, pd, poolConfig);

		pd->uBlockLimitLength = constructEMTPool(&poolConfig->sPool, poolConfig->uBlockCount);
		metaLength += pd->uBlockLimitLength;
		*pMemLen += poolConfig->uBlockLength * poolConfig->uBlockCount;
	}

	d->uId = metaLength;
	return metaLength + (*pMemLen >> kEMTMultiPoolPageShift) * sizeof(uint8_t);
}

void EMTMultiPool_destruct(PEMTMULTIPOOL pThis)
{
	EMT_D(EMTMULTIPOOL);
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		destructEMTPool(&poolConfig->sPool);
	}
}

void EMTMultiPool_init(PEMTMULTIPOOL pThis, void * pMeta, void * pPool)
{
	EMT_D(EMTMULTIPOOL);
	uint8_t * meta = (uint8_t *)pMeta;
	uint8_t * mem = (uint8_t *)pPool;
	uint8_t * memMap = meta + d->uId;
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	d->pMemMap = memMap;
	d->pMeta = (PEMTMULTIPOOLMETA)meta;
	meta += sizeof(*d->pMeta);

	do
	{
		d->uId = d->pMeta->uNextId;
	} while (rt_cmpXchg32(&d->pMeta->uNextId, d->uId + 1, d->uId) != d->uId || d->uId == 0);

	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		EMT_PRIVATE(EMTMULTIPOOLCONFIG, pd, poolConfig);
		const uint32_t memMapCount = poolConfig->uBlockLength * poolConfig->uBlockCount >> kEMTMultiPoolPageShift;

		rt_memset(memMap, pThis->uPoolCount - (poolConfigEnd - poolConfig), memMapCount);
		poolConfig->sPool.init(&poolConfig->sPool, d->uId, poolConfig->uBlockLength, meta, mem);
		mem += poolConfig->uBlockLength * poolConfig->uBlockCount;
		meta += pd->uBlockLimitLength;
		memMap += memMapCount;
		pd->uBlockLimitLength = poolConfig->uBlockLength * poolConfig->uBlockLimit;
	}

	d->pMem = pPool;
	d->pMemEnd = mem;
	d->uBlockLimitLength = (poolConfigEnd - 1)->uBlockLength * (poolConfigEnd - 1)->uBlockCount;
}

const uint32_t EMTMultiPool_id(PEMTMULTIPOOL pThis)
{
	EMT_D(EMTMULTIPOOL);
	return d->uId;
}

const PEMTPOOL EMTMultiPool_pool(PEMTMULTIPOOL pThis, const uint32_t uPool)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, uPool);
	return poolConfig ? &poolConfig->sPool : 0;
}

const PEMTPOOL EMTMultiPool_poolByMem(PEMTMULTIPOOL pThis, void * pMem)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfigByMem(pThis, pMem);
	return poolConfig ? &poolConfig->sPool : 0;
}

const uint32_t EMTMultiPool_length(PEMTMULTIPOOL pThis, void * pMem)
{
	PEMTPOOL pool = EMTMultiPool_poolByMem(pThis, pMem);
	return pool ? pool->length(pool, pMem) : 0;
}

void * EMTMultiPool_alloc(PEMTMULTIPOOL pThis, const uint32_t uMemLen)
{
	EMT_D(EMTMULTIPOOL);
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	if (uMemLen > d->uBlockLimitLength)
		return 0;

	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		EMT_PRIVATE(EMTMULTIPOOLCONFIG, pd, poolConfig);
		if (uMemLen <= pd->uBlockLimitLength)
			return poolConfig->sPool.alloc(&poolConfig->sPool, uMemLen);
	}

	return 0;
}

void EMTMultiPool_free(PEMTMULTIPOOL pThis, void * pMem)
{
	PEMTPOOL pool = EMTMultiPool_poolByMem(pThis, pMem);
	pool ? pool->free(pool, pMem) : 0;
}

void EMTMultiPool_freeAll(PEMTMULTIPOOL pThis, const uint32_t uId)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		poolConfig->sPool.freeAll(&poolConfig->sPool, uId);
	}
}

const uint32_t EMTMultiPool_transfer(PEMTMULTIPOOL pThis, void * pMem, const uint32_t uToId)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfigByMem(pThis, pMem);

	if (poolConfig)
	{
		const uint32_t poolId = (poolConfig - EMTMultiPool_poolConfig(pThis, 0)) << kEMTMultiPoolPoolIdShift;
		const uint32_t token = poolConfig->sPool.transfer(&poolConfig->sPool, pMem, uToId) & kEMTMultiPoolTokenMask;

		return poolId | token;
	}

	return kEMTMultiPoolInvalidToken;
}

void * EMTMultiPool_take(PEMTMULTIPOOL pThis, const uint32_t uToken)
{
	EMT_D(EMTMULTIPOOL);
	const uint8_t poolId = uToken >> kEMTMultiPoolPoolIdShift;
	const uint32_t token = uToken & kEMTMultiPoolTokenMask;
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, poolId);

	if (poolConfig)
	{
		return poolConfig->sPool.take(&poolConfig->sPool, token);
	}

	return 0;
}

uint32_t constructEMTMultiPool(PEMTMULTIPOOL pEMTMultiPool, uint32_t * pMemLen)
{
	return EMTMultiPool_construct(pEMTMultiPool, pMemLen);
}

void destructEMTMultiPool(PEMTMULTIPOOL pEMTMultiPool)
{
	EMTMultiPool_destruct(pEMTMultiPool);
}
