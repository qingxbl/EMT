#define EMTIMPL_POOL
#include "EMTPool.h"

#pragma pack(push, 1)
struct _EMTPOOLBLOCKMETA
{
	volatile uint32_t uOwner;
	volatile uint32_t uLen;
	uint32_t uAllocLen;
};

struct _EMTPOOLMETA
{
	volatile uint32_t uNextBlock;

	uint32_t uBlockLen;
	uint32_t uBlockCount;
};
#pragma pack(pop)

enum
{
	kEMTPoolMagicNumUninit = 0,
	kEMTPoolMagicNumInit = ~0,
	kEMTPoolMagicNumInited = 2,

	kEMTPoolNoError = 0,
	kEMTPoolError = 0x80000000,
	kEMTPoolOutOfRange,
	kEMTPoolNotOwner,
};

static const uint32_t EMTPool_blockFromAddress(PEMTPOOL pThis, void * pMem)
{
	return (uint32_t)(((uint8_t *)pMem - (uint8_t *)pThis->pPool) / pThis->pMeta->uBlockLen);
}

static int32_t EMTPool_validation(PEMTPOOL pThis, void * pMem)
{
	const uint32_t uBlock = EMTPool_blockFromAddress(pThis, pMem);
	PEMTPOOLBLOCKMETA pBlockMeta = pThis->pBlockMeta + uBlock;

	if (uBlock >= pThis->pMeta->uBlockCount)
		return kEMTPoolOutOfRange;

	//if (pBlockMeta->owner != pThis->uId)
	//	return kEMTPoolNotOwner;

	return kEMTPoolNoError;
}

void EMTPool_calcMetaSize(const uint32_t uBlockCount, const uint32_t uBlockLen, uint32_t * pMetaLen, uint32_t * pMemLen)
{
	*pMetaLen = sizeof(EMTPOOLMETA) + sizeof(EMTPOOLBLOCKMETA) * uBlockCount;
	*pMemLen = uBlockLen * uBlockCount;
}

void EMTPool_construct(PEMTPOOL pThis, const uint32_t uId, const uint32_t uBlockCount, const uint32_t uBlockLen, const uint32_t uBlockInit, void * pMeta, void * pPool)
{
	volatile uint32_t * pInitStatus;
	uint32_t i;

	pThis->pMeta = (PEMTPOOLMETA)pMeta;
	pThis->pBlockMeta = (PEMTPOOLBLOCKMETA)(pThis->pMeta + 1);
	pThis->pPool = pPool;
	pThis->uId = uId;

	pInitStatus = &pThis->pMeta->uBlockLen;

	i = rt_cmpXchg32(pInitStatus, kEMTPoolMagicNumInit, kEMTPoolMagicNumUninit);
	while (i != kEMTPoolMagicNumUninit && *pInitStatus == kEMTPoolMagicNumInit);

	if (i != kEMTPoolMagicNumUninit)
		return;

	pThis->pMeta->uNextBlock = 0;
	pThis->pMeta->uBlockLen = uBlockLen;
	pThis->pMeta->uBlockCount = uBlockCount;
	rt_memset(pThis->pBlockMeta, 0, pThis->pMeta->uBlockCount * sizeof(*pThis->pBlockMeta));
	for (i = 0; i < uBlockCount; i += uBlockInit)
		pThis->pBlockMeta[i].uLen = uBlockInit;
}

void EMTPool_destruct(PEMTPOOL pThis)
{
	EMTPool_freeAll(pThis, pThis->uId);
}

const uint32_t EMTPool_id(PEMTPOOL pThis)
{
	return pThis->uId;
}

void * EMTPool_address(PEMTPOOL pThis)
{
	return pThis->pPool;
}

const uint32_t EMTPool_poolLength(PEMTPOOL pThis)
{
	return pThis->pMeta->uBlockLen * pThis->pMeta->uBlockCount;
}

const uint32_t EMTPool_blockLength(PEMTPOOL pThis)
{
	return pThis->pMeta->uBlockLen;
}

const uint32_t EMTPool_blockCount(PEMTPOOL pThis)
{
	return pThis->pMeta->uBlockCount;
}

const uint32_t EMTPool_length(PEMTPOOL pThis, void * pMem)
{
	const uint32_t uBlock = EMTPool_blockFromAddress(pThis, pMem);
	PEMTPOOLBLOCKMETA pBlockMeta = pThis->pBlockMeta + uBlock;
	return pBlockMeta->uAllocLen;
}

void * EMTPool_alloc(PEMTPOOL pThis, const uint32_t uMemLen)
{
	const uint32_t uBlocks = (uMemLen + pThis->pMeta->uBlockLen - 1) / pThis->pMeta->uBlockLen;
	PEMTPOOLBLOCKMETA pBlockMeta = 0;
	uint32_t uRound = 3;
	while ((pBlockMeta == 0 || pBlockMeta->uLen < uBlocks) && uRound)
	{
		const uint32_t uBlockCurP = pBlockMeta ? (uint32_t)(pBlockMeta - pThis->pBlockMeta + pBlockMeta->uLen) : pThis->pMeta->uNextBlock;
		const uint32_t uBlockCur = uBlockCurP < pThis->pMeta->uBlockCount ? uBlockCurP : 0;
		PEMTPOOLBLOCKMETA pBlockMetaCur = pThis->pBlockMeta + uBlockCur;
		const uint32_t uBlockNextP = uBlockCur + pBlockMetaCur->uLen;
		const uint32_t uBlockNext = uBlockNextP < pThis->pMeta->uBlockCount ? uBlockNextP : 0;

		const uint32_t bSuccess = rt_cmpXchg32(&pThis->pMeta->uNextBlock, uBlockNext, uBlockCur) == uBlockCur
			&& rt_cmpXchg32(&pBlockMetaCur->uOwner, pThis->uId, 0) == 0;

		if (pBlockMeta != 0 && (!bSuccess || uBlockCur != uBlockCurP))
		{
			pBlockMeta->uOwner = 0;
			pBlockMeta = 0;
		}

		if (uBlockNext != uBlockNextP)
		{
			--uRound;
		}

		if (bSuccess)
		{
			if (pBlockMeta != 0)
			{
				pBlockMeta->uLen += pBlockMetaCur->uLen;
			}
			else
			{
				pBlockMeta = pBlockMetaCur;
			}
		}
	}

	if (pBlockMeta)
	{
		if (pBlockMeta->uLen > uBlocks)
		{
			PEMTPOOLBLOCKMETA pBlockMetaCur = pBlockMeta + uBlocks;
			pBlockMetaCur->uLen = pBlockMeta->uLen - uBlocks;
			pBlockMetaCur->uOwner = 0;
			pBlockMeta->uLen = uBlocks;
		}
		if (pBlockMeta->uLen == uBlocks)
		{
			pBlockMeta->uAllocLen = uMemLen;
		}

		if (pBlockMeta->uLen < uBlocks)
		{
			pBlockMeta->uOwner = 0;
			pBlockMeta = 0;
		}
	}

	return pBlockMeta ? (uint8_t *)pThis->pPool + pThis->pMeta->uBlockLen * (pBlockMeta - pThis->pBlockMeta) : 0;
}

void EMTPool_free(PEMTPOOL pThis, void * pMem)
{
	const uint32_t uBlock = EMTPool_blockFromAddress(pThis, pMem);
	PEMTPOOLBLOCKMETA pBlockMeta = pThis->pBlockMeta + uBlock;

	if (EMTPool_validation(pThis, pMem) != kEMTPoolNoError)
		return;

	pBlockMeta->uOwner = 0;
}

void EMTPool_freeAll(PEMTPOOL pThis, const uint32_t uId)
{
	PEMTPOOLBLOCKMETA pBlockMetaCur = pThis->pBlockMeta;
	PEMTPOOLBLOCKMETA pBlockMetaEnd = pThis->pBlockMeta + pThis->pMeta->uBlockCount;

	do
	{
		PEMTPOOLBLOCKMETA pBlockMeta = pBlockMetaCur;
		pBlockMetaCur += pBlockMeta->uLen;

		if (pBlockMeta->uOwner == uId)
			pBlockMeta->uOwner = 0;
	} while (pBlockMetaCur < pBlockMetaEnd);
}

const uint32_t EMTPool_transfer(PEMTPOOL pThis, void * pMem, const uint32_t uToId)
{
	const uint32_t uBlock = EMTPool_blockFromAddress(pThis, pMem);
	PEMTPOOLBLOCKMETA pBlockMeta = pThis->pBlockMeta + uBlock;

	if (EMTPool_validation(pThis, pMem) != kEMTPoolNoError)
		return 0;

	pBlockMeta->uOwner = uToId;

	return uBlock;
}

void * EMTPool_take(PEMTPOOL pThis, const uint32_t uToken)
{
	void * pMem = (uint8_t *)pThis->pPool + uToken * pThis->pMeta->uBlockLen;

	if (EMTPool_validation(pThis, pMem) != kEMTPoolNoError)
		return 0;

	return pMem;
}

PCEMTPOOLOPS emtPool(void)
{
	static const EMTPOOLOPS sOps =
	{
		EMTPool_calcMetaSize,
		EMTPool_construct,
		EMTPool_destruct,
		EMTPool_id,
		EMTPool_address,
		EMTPool_poolLength,
		EMTPool_blockLength,
		EMTPool_blockCount,
		EMTPool_length,
		EMTPool_alloc,
		EMTPool_free,
		EMTPool_freeAll,
		EMTPool_transfer,
		EMTPool_take,
	};

	return &sOps;
}
