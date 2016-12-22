#define EMTIMPL_MULTIPOOL
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

void EMTMultiPool_calcMetaSize(PEMTMULTIPOOL pThis, uint32_t * pMetaLen, uint32_t * pMemLen)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	*pMetaLen = sizeof(EMTMULTIPOOLMETA);
	*pMemLen = 0;
	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		uint32_t poolMetaLen, poolMemLen;
		EMTPool_calcMetaSize(poolConfig->uBlockCount, poolConfig->uBlockLength, &poolMetaLen, &poolMemLen);
		*pMetaLen += poolMetaLen + (poolConfig->uBlockLength * poolConfig->uBlockCount >> kEMTMultiPoolPageShift);
		*pMemLen += poolMemLen;
	}
}

void EMTMultiPool_construct(PEMTMULTIPOOL pThis, void * pMeta, void * pPool)
{
	uint8_t * meta = (uint8_t *)pMeta;
	uint8_t * mem = (uint8_t *)pPool;
	PEMTMULTIPOOLCONFIG poolConfig;
	uint32_t i;

	pThis->pMeta = (PEMTMULTIPOOLMETA)meta;
	meta += sizeof(*pThis->pMeta);

	do
	{
		pThis->uId = pThis->pMeta->uNextId;
	} while (rt_cmpXchg32(&pThis->pMeta->uNextId, pThis->uId + 1, pThis->uId) != pThis->uId || pThis->uId == 0);

	for (i = 0; i < pThis->uPoolCount; ++i)
	{
		poolConfig = EMTMultiPool_poolConfig(pThis, i);
		uint32_t poolMetaLen, poolMemLen;
		EMTPool_calcMetaSize(poolConfig->uBlockCount, poolConfig->uBlockLength, &poolMetaLen, &poolMemLen);

		EMTPool_construct(&poolConfig->sPool, pThis->uId, poolConfig->uBlockCount, poolConfig->uBlockLength, poolConfig->uBlockLimit, meta, mem);
		meta += poolMetaLen;
		mem += poolMemLen;
		poolConfig->uBlockLimitLength = poolConfig->uBlockLength * poolConfig->uBlockLimit;
	}

	pThis->pMemMap = meta;
	for (i = 0; i < pThis->uPoolCount; ++i)
	{
		poolConfig = EMTMultiPool_poolConfig(pThis, i);
		const uint32_t memMapCount = poolConfig->uBlockLength * poolConfig->uBlockCount >> kEMTMultiPoolPageShift;

		rt_memset(meta, i, memMapCount);
		meta += memMapCount;
	}

	poolConfig = EMTMultiPool_poolConfig(pThis, pThis->uPoolCount - 1);
	pThis->pMem = pPool;
	pThis->pMemEnd = mem;
	pThis->uBlockLimitLength = poolConfig->uBlockLength * poolConfig->uBlockCount;
}

void EMTMultiPool_destruct(PEMTMULTIPOOL pThis)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		EMTPool_destruct(&poolConfig->sPool);
	}
}

const uint32_t EMTMultiPool_id(PEMTMULTIPOOL pThis)
{
	return pThis->uId;
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
	return pool ? EMTPool_length(pool, pMem) : 0;
}

void * EMTMultiPool_alloc(PEMTMULTIPOOL pThis, const uint32_t uMemLen)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	if (uMemLen > pThis->uBlockLimitLength)
		return 0;

	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		if (uMemLen <= poolConfig->uBlockLimitLength)
			return EMTPool_alloc(&poolConfig->sPool, uMemLen);
	}

	return 0;
}

void EMTMultiPool_free(PEMTMULTIPOOL pThis, void * pMem)
{
	PEMTPOOL pool = EMTMultiPool_poolByMem(pThis, pMem);
	pool ? EMTPool_free(pool, pMem) : 0;
}

void EMTMultiPool_freeAll(PEMTMULTIPOOL pThis, const uint32_t uId)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, 0);
	PEMTMULTIPOOLCONFIG poolConfigEnd = poolConfig + pThis->uPoolCount;

	for (; poolConfig < poolConfigEnd; ++poolConfig)
	{
		EMTPool_freeAll(&poolConfig->sPool, uId);
	}
}

const uint32_t EMTMultiPool_transfer(PEMTMULTIPOOL pThis, void * pMem, const uint32_t uToId)
{
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfigByMem(pThis, pMem);

	if (poolConfig)
	{
		const uint32_t poolId = (poolConfig - EMTMultiPool_poolConfig(pThis, 0)) << kEMTMultiPoolPoolIdShift;
		const uint32_t token = EMTPool_transfer(&poolConfig->sPool, pMem, uToId) & kEMTMultiPoolTokenMask;

		return poolId | token;
	}

	return kEMTMultiPoolInvalidToken;
}

void * EMTMultiPool_take(PEMTMULTIPOOL pThis, const uint32_t uToken)
{
	const uint8_t poolId = uToken >> kEMTMultiPoolPoolIdShift;
	const uint32_t token = uToken & kEMTMultiPoolTokenMask;
	PEMTMULTIPOOLCONFIG poolConfig = EMTMultiPool_poolConfig(pThis, poolId);

	if (poolConfig)
	{
		return EMTPool_take(&poolConfig->sPool, token);
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
