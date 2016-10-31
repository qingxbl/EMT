#include "EMTPool.h"

enum
{
	kEMTPoolMagicNumUninit = 0,
	kEMTPoolMagicNumInit = 1,
	kEMTPoolMagicNumInited = 2,

	kEMTPoolNoError = 0,
	kEMTPoolError = 0x80000000,
	kEMTPoolOutOfRange,
	kEMTPoolNotOwner,
};

static void EMTPool_construct(PEMTPOOL pThis, void * pMem, const uint32_t uMemLen, const uint32_t uBlockLen);
static void EMTPool_destruct(PEMTPOOL pThis);
static void EMTPool_init(PEMTPOOL pThis, const uint32_t uMemLen, const uint32_t uBlockLen);
static const uint32_t EMTPool_id(PEMTPOOL pThis);
static void * EMTPool_alloc(PEMTPOOL pThis, const uint32_t uMemLen);
static void EMTPool_free(PEMTPOOL pThis, void * pMem);
static void EMTPool_freeAll(PEMTPOOL pThis, const uint32_t uId);
static const uint32_t EMTPool_transfer(PEMTPOOL pThis, void * pMem, const uint32_t uToId);
static void * EMTPool_take(PEMTPOOL pThis, const uint32_t uToken);

typedef struct _EMTPOOLPRIVATE EMTPOOLPRIVATE, *PEMTPOOLPRIVATE;
typedef struct _EMTPOOLBLOCKMETAINFO EMTPOOLBLOCKMETAINFO, *PEMTPOOLBLOCKMETAINFO;
typedef struct _EMTPOOLMETAINFO EMTPOOLMETAINFO, *PEMTPOOLMETAINFO;

struct _EMTPOOLPRIVATE
{
	void * pMem;

	PEMTPOOLMETAINFO pMetaInfo;
	PEMTPOOLBLOCKMETAINFO pBlockMetaInfo;

	uint32_t uId;
};

struct _EMTPOOLBLOCKMETAINFO
{
	volatile uint32_t owner;
	volatile uint32_t len;
};

struct _EMTPOOLMETAINFO
{
	volatile uint32_t uMagic;

	volatile uint32_t uNextId;
	volatile uint32_t uNextBlock;

	uint32_t uMemLen;
	uint32_t uBlockLen;

	uint32_t uBlockStart;
	uint32_t uBlockCount;
};

#define EMT_D(TYPE) TYPE##PRIVATE * d = (TYPE##PRIVATE *)pThis->reserved

static const uint32_t EMTPool_blockFromAddress(PEMTPOOL pThis, void * pMem)
{
	EMT_D(EMTPOOL);

	return (uint32_t)(((uint8_t *)pMem - (uint8_t *)d->pMem) / d->pMetaInfo->uBlockLen);
}

static int32_t EMTPool_validation(PEMTPOOL pThis, void * pMem)
{
	EMT_D(EMTPOOL);
	const uint32_t uBlock = EMTPool_blockFromAddress(pThis, pMem);
	PEMTPOOLBLOCKMETAINFO pBlockMetaInfo = d->pBlockMetaInfo + uBlock;

	if (uBlock < d->pMetaInfo->uBlockStart || uBlock >= d->pMetaInfo->uBlockCount)
		return kEMTPoolOutOfRange;

	//if (pBlockMetaInfo->owner != d->uId)
	//	return kEMTPoolNotOwner;

	return kEMTPoolNoError;
}

static void EMTPool_construct(PEMTPOOL pThis, void * pMem, const uint32_t uMemLen, const uint32_t uBlockLen)
{
	EMT_D(EMTPOOL);

	pThis->id = EMTPool_id;
	pThis->alloc = EMTPool_alloc;
	pThis->free = EMTPool_free;
	pThis->transfer = EMTPool_transfer;
	pThis->take = EMTPool_take;

	d->pMem = pMem;
	d->pMetaInfo = (PEMTPOOLMETAINFO)d->pMem;
	d->pBlockMetaInfo = (PEMTPOOLBLOCKMETAINFO)(d->pMetaInfo + 1);

	EMTPool_init(pThis, uMemLen, uBlockLen);

	for (;;)
	{
		d->uId = d->pMetaInfo->uNextId;

		if (rt_cmpXchg32(&d->pMetaInfo->uNextId, d->uId + 1, d->uId) == d->uId)
			break;
	}
}

static void EMTPool_destruct(PEMTPOOL pThis)
{
	EMT_D(EMTPOOL);

	EMTPool_freeAll(pThis, d->uId);
}

static void EMTPool_init(PEMTPOOL pThis, const uint32_t uMemLen, const uint32_t uBlockLen)
{
	EMT_D(EMTPOOL);

	const uint32_t uMagicNum = rt_cmpXchg32(&d->pMetaInfo->uMagic, kEMTPoolMagicNumInit, kEMTPoolMagicNumUninit);
	while (uMagicNum != kEMTPoolMagicNumUninit && uMagicNum == kEMTPoolMagicNumInit);

	if (uMagicNum != kEMTPoolMagicNumUninit)
		return;

	d->pMetaInfo->uMemLen = uMemLen;
	d->pMetaInfo->uBlockLen = uBlockLen;
	d->pMetaInfo->uBlockCount = d->pMetaInfo->uMemLen / d->pMetaInfo->uBlockLen;
	d->pMetaInfo->uBlockStart = (sizeof(*d->pMetaInfo) + d->pMetaInfo->uBlockCount * sizeof(*d->pBlockMetaInfo) + uBlockLen - 1) / uBlockLen;
	d->pMetaInfo->uNextId = 1;
	d->pMetaInfo->uNextBlock = d->pMetaInfo->uBlockStart;

	rt_memset(d->pBlockMetaInfo, 0, d->pMetaInfo->uBlockCount * sizeof(*d->pBlockMetaInfo));
	d->pBlockMetaInfo[d->pMetaInfo->uBlockStart].len = d->pMetaInfo->uBlockCount - d->pMetaInfo->uBlockStart;

	d->pMetaInfo->uMagic = kEMTPoolMagicNumInited;
}

static const uint32_t EMTPool_id(PEMTPOOL pThis)
{
	EMT_D(EMTPOOL);
	return d->uId;
}

static void * EMTPool_alloc(PEMTPOOL pThis, const uint32_t uMemLen)
{
	EMT_D(EMTPOOL);

	const uint32_t uBlocks = (uMemLen + d->pMetaInfo->uBlockLen - 1) / d->pMetaInfo->uBlockLen;
	PEMTPOOLBLOCKMETAINFO pBlockMetaInfo = 0;
	while (pBlockMetaInfo == 0 || pBlockMetaInfo->len < uBlocks)
	{
		const uint32_t uBlockCur = d->pMetaInfo->uNextBlock;
		PEMTPOOLBLOCKMETAINFO pBlockMetaInfoCur = d->pBlockMetaInfo + uBlockCur;
		const uint32_t uBlockNextP = uBlockCur + pBlockMetaInfoCur->len;
		const uint32_t uBlockNext = uBlockNextP < d->pMetaInfo->uBlockCount ? uBlockNextP : d->pMetaInfo->uBlockStart;

		if (rt_cmpXchg32(&d->pMetaInfo->uNextBlock, uBlockNext, uBlockCur) == uBlockCur
			&& rt_cmpXchg32(&pBlockMetaInfoCur->owner, d->uId, 0) == 0)
		{
			if (pBlockMetaInfo != 0)
			{
				pBlockMetaInfo->len += pBlockMetaInfoCur->len;
			}
			else
			{
				pBlockMetaInfo = pBlockMetaInfoCur;
			}
		}
		else
		{
			if (pBlockMetaInfo != 0)
			{
				pBlockMetaInfo->owner = 0;
				pBlockMetaInfo = 0;
			}
		}
	}

	if (pBlockMetaInfo->len > uBlocks)
	{
		const uint32_t uShrinkLen = pBlockMetaInfo->len - uBlocks;
		PEMTPOOLBLOCKMETAINFO pBlockMetaInfoCur = pBlockMetaInfo + uShrinkLen;
		pBlockMetaInfoCur->len = uShrinkLen;
		pBlockMetaInfoCur->owner = 0;
	}

	return (uint8_t *)d->pMem + (pBlockMetaInfo - d->pBlockMetaInfo) * d->pMetaInfo->uBlockLen;
}

static void EMTPool_free(PEMTPOOL pThis, void * pMem)
{
	EMT_D(EMTPOOL);
	const uint32_t uBlock = EMTPool_blockFromAddress(pThis, pMem);
	PEMTPOOLBLOCKMETAINFO pBlockMetaInfo = d->pBlockMetaInfo + uBlock;

	if (EMTPool_validation(pThis, pMem) != kEMTPoolNoError)
		return;

	pBlockMetaInfo->owner = 0;
}

void EMTPool_freeAll(PEMTPOOL pThis, const uint32_t uId)
{

}

static const uint32_t EMTPool_transfer(PEMTPOOL pThis, void * pMem, const uint32_t uToId)
{
	EMT_D(EMTPOOL);
	const uint32_t uBlock = EMTPool_blockFromAddress(pThis, pMem);
	PEMTPOOLBLOCKMETAINFO pBlockMetaInfo = d->pBlockMetaInfo + uBlock;

	if (EMTPool_validation(pThis, pMem) != kEMTPoolNoError)
		return 0;

	pBlockMetaInfo->owner = uToId;

	return uBlock;
}

static void * EMTPool_take(PEMTPOOL pThis, const uint32_t uToken)
{
	EMT_D(EMTPOOL);
	void * pMem = (uint8_t *)d->pMem + uToken * d->pMetaInfo->uBlockLen;

	if (EMTPool_validation(pThis, pMem) != kEMTPoolNoError)
		return 0;

	return pMem;
}

void constructEMTPool(PEMTPOOL pEMTPool, void * pMem, const uint32_t uMemLen, const uint32_t uBlockLen)
{
	EMTPool_construct(pEMTPool, pMem, uMemLen, uBlockLen);
}

void destructEMTPool(PEMTPOOL pEMTPool)
{
	EMTPool_destruct(pEMTPool);
}
