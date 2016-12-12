#include "EMTMultiPool.h"

#pragma pack(push, 1)
struct _EMTMULTIPOOLMETA
{
	volatile uint32_t uNextId;
};
#pragma pack(pop)

enum
{
	kEMTMultiPoolPageShift = 12,
	kEMTMultiPoolPoolIdShift = 24,
	kEMTMultiPoolTokenMask = (1 << 24) - 1,

	kEMTMultiPoolInvalidToken = ~0x0,
};

static void EMTMultiPool_calcMetaSize(PEMTMULTIPOOL pThis, uint32_t * pMetaLen, uint32_t * pMemLen);
static void EMTMultiPool_construct(PEMTMULTIPOOL pThis, void * pMeta, void * pPool);
static void EMTMultiPool_destruct(PEMTMULTIPOOL pThis);
static const uint32_t EMTMultiPool_id(PEMTMULTIPOOL pThis);
static const PEMTPOOL EMTMultiPool_pool(PEMTMULTIPOOL pThis, const uint32_t uPool);
static const PEMTPOOL EMTMultiPool_poolByMem(PEMTMULTIPOOL pThis, void * pMem);
static const uint32_t EMTMultiPool_length(PEMTMULTIPOOL pThis, void * pMem);
static void * EMTMultiPool_alloc(PEMTMULTIPOOL pThis, const uint32_t uMemLen);
static void EMTMultiPool_free(PEMTMULTIPOOL pThis, void * pMem);
static void EMTMultiPool_freeAll(PEMTMULTIPOOL pThis, const uint32_t uId);
static const uint32_t EMTMultiPool_transfer(PEMTMULTIPOOL pThis, void * pMem, const uint32_t uToId);
static void * EMTMultiPool_take(PEMTMULTIPOOL pThis, const uint32_t uToken);

static PEMTMULTIPOOLCONFIG EMTMultiPool_poolConfig(PEMTMULTIPOOL pThis, const uint32_t uPool)
{
	return uPool < pThis->uPoolCount ? (PEMTMULTIPOOLCONFIG)(pThis + 1) + uPool : 0;
}

static PEMTMULTIPOOLCONFIG EMTMultiPool_poolConfigByMem(PEMTMULTIPOOL pThis, void * pMem)
{
	if (pMem < pThis->pMem || pMem > pThis->pMemEnd)
		return 0;

	return EMTMultiPool_poolConfig(pThis, pThis->pMemMap[((uint8_t *)pMem - (uint8_t *)pThis->pMem) >> kEMTMultiPoolPageShift]);
}

static void EMTMultiPool_calcMetaSize(PEMTMULTIPOOL pThis, uint32_t * pMetaLen, uint32_t * pMemLen)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	*pMetaLen = sizeof(EMTMULTIPOOLMETA);
	*pMemLen = 0;
	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		uint32_t poolMetaLen, poolMemLen;
		emtPool()->calcMetaSize(poolConfig->uBlockCount, poolConfig->uBlockLength, &poolMetaLen, &poolMemLen);
		*pMetaLen += poolMetaLen;
		*pMemLen += poolMemLen;
	}
}

static void EMTMultiPool_construct(PEMTMULTIPOOL pThis, void * pMeta, void * pPool)
{
	uint8_t * meta = (uint8_t *)pMeta;
	uint8_t * mem = (uint8_t *)pPool;
	uint8_t * memMap = meta + pThis->uId;
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	pThis->pMemMap = memMap;
	pThis->pMeta = (PEMTMULTIPOOLMETA)meta;
	meta += sizeof(*pThis->pMeta);

	do
	{
		pThis->uId = pThis->pMeta->uNextId;
	} while (rt_cmpXchg32(&pThis->pMeta->uNextId, pThis->uId + 1, pThis->uId) != pThis->uId || pThis->uId == 0);

	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		const uint32_t memMapCount = poolConfig->uBlockLength * poolConfig->uBlockCount >> kEMTMultiPoolPageShift;
		uint32_t poolMetaLen, poolMemLen;
		emtPool()->calcMetaSize(poolConfig->uBlockCount, poolConfig->uBlockLength, &poolMetaLen, &poolMemLen);

		rt_memset(memMap, pThis->uPoolCount - (poolConfigEnd - poolConfig), memMapCount);
		emtPool()->construct(&poolConfig->sPool, pThis->uId, poolConfig->uBlockCount, poolConfig->uBlockLength, meta, mem);
		meta += poolMetaLen;
		mem += poolMemLen;
		memMap += memMapCount;
		poolConfig->uBlockLimitLength = poolConfig->uBlockLength * poolConfig->uBlockLimit;
	}

	pThis->pMem = pPool;
	pThis->pMemEnd = mem;
	pThis->uBlockLimitLength = (poolConfigEnd - 1)->uBlockLength * (poolConfigEnd - 1)->uBlockCount;
}

static void EMTMultiPool_destruct(PEMTMULTIPOOL pThis)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		emtPool()->destruct(&poolConfig->sPool);
	}
}

static const uint32_t EMTMultiPool_id(PEMTMULTIPOOL pThis)
{
	return pThis->uId;
}

static const PEMTPOOL EMTMultiPool_pool(PEMTMULTIPOOL pThis, const uint32_t uPool)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, uPool);
	return poolConfig ? &poolConfig->sPool : 0;
}

static const PEMTPOOL EMTMultiPool_poolByMem(PEMTMULTIPOOL pThis, void * pMem)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfigByMem(pThis, pMem);
	return poolConfig ? &poolConfig->sPool : 0;
}

static const uint32_t EMTMultiPool_length(PEMTMULTIPOOL pThis, void * pMem)
{
	PEMTPOOL pool = EMTMultiPool_poolByMem(pThis, pMem);
	return pool ? emtPool()->length(pool, pMem) : 0;
}

static void * EMTMultiPool_alloc(PEMTMULTIPOOL pThis, const uint32_t uMemLen)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	if (uMemLen > pThis->uBlockLimitLength)
		return 0;

	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		if (uMemLen <= poolConfig->uBlockLimitLength)
			return emtPool()->alloc(&poolConfig->sPool, uMemLen);
	}

	return 0;
}

static void EMTMultiPool_free(PEMTMULTIPOOL pThis, void * pMem)
{
	PEMTPOOL pool = EMTMultiPool_poolByMem(pThis, pMem);
	pool ? emtPool()->free(pool, pMem) : 0;
}

static void EMTMultiPool_freeAll(PEMTMULTIPOOL pThis, const uint32_t uId)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		emtPool()->freeAll(&poolConfig->sPool, uId);
	}
}

static const uint32_t EMTMultiPool_transfer(PEMTMULTIPOOL pThis, void * pMem, const uint32_t uToId)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfigByMem(pThis, pMem);

	if (poolConfig)
	{
		const uint32_t poolId = (poolConfig - EMTMultiPool_poolConfig(pThis, 0)) << kEMTMultiPoolPoolIdShift;
		const uint32_t token = emtPool()->transfer(&poolConfig->sPool, pMem, uToId) & kEMTMultiPoolTokenMask;

		return poolId | token;
	}

	return kEMTMultiPoolInvalidToken;
}

static void * EMTMultiPool_take(PEMTMULTIPOOL pThis, const uint32_t uToken)
{
	const uint8_t poolId = uToken >> kEMTMultiPoolPoolIdShift;
	const uint32_t token = uToken & kEMTMultiPoolTokenMask;
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, poolId);

	if (poolConfig)
	{
		return emtPool()->take(&poolConfig->sPool, token);
	}

	return 0;
}

PCEMTMULTIPOOLOPS emtMultiPool(void)
{
	static const EMTMULTIPOOLOPS sOps =
	{
		EMTMultiPool_calcMetaSize,
		EMTMultiPool_construct,
		EMTMultiPool_destruct,
		EMTMultiPool_id,
		EMTMultiPool_pool,
		EMTMultiPool_poolByMem,
		EMTMultiPool_length,
		EMTMultiPool_alloc,
		EMTMultiPool_free,
		EMTMultiPool_freeAll,
		EMTMultiPool_transfer,
		EMTMultiPool_take,
	};

	return &sOps;
}
