#include "EMTPool.h"

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

static void EMTPool_construct(PEMTPOOL pThis, const uint32_t id, void * pMem, const uint32_t uMetaOffset, const uint32_t uMemLen, const uint32_t uBlockLen);
static void EMTPool_destruct(PEMTPOOL pThis);
static void EMTPool_init(PEMTPOOL pThis, const uint32_t uMemLen, const uint32_t uBlockLen);
static const uint32_t EMTPool_id(PEMTPOOL pThis);
static void * EMTPool_address(PEMTPOOL pThis);
static const uint32_t EMTPool_memLength(PEMTPOOL pThis);
static const uint32_t EMTPool_blockLength(PEMTPOOL pThis);
static const uint32_t EMTPool_length(PEMTPOOL pThis, void * pMem);
static void * EMTPool_alloc(PEMTPOOL pThis, const uint32_t uMemLen);
static void EMTPool_free(PEMTPOOL pThis, void * pMem);
static void EMTPool_freeAll(PEMTPOOL pThis, const uint32_t uId);
static const uint32_t EMTPool_transfer(PEMTPOOL pThis, void * pMem, const uint32_t uToId);
static void * EMTPool_take(PEMTPOOL pThis, const uint32_t uToken);

typedef struct _EMTPOOLPRIVATE EMTPOOLPRIVATE, *PEMTPOOLPRIVATE;
typedef struct _EMTPOOLBLOCKMETA EMTPOOLBLOCKMETA, *PEMTPOOLBLOCKMETA;
typedef struct _EMTPOOLMETA EMTPOOLMETA, *PEMTPOOLMETA;

struct _EMTPOOLPRIVATE
{
	void * pMem;

	PEMTPOOLMETA pMeta;
	PEMTPOOLBLOCKMETA pBlockMeta;

	uint32_t uId;
};

struct _EMTPOOLBLOCKMETA
{
	volatile uint32_t owner;
	volatile uint32_t len;
	uint32_t allocLen;
};

struct _EMTPOOLMETA
{
	volatile uint32_t uNextBlock;

	uint32_t uMemLen;
	uint32_t uBlockLen;

	uint32_t uBlockStart;
	uint32_t uBlockCount;
};

#define EMT_D(TYPE) TYPE##PRIVATE * d = (TYPE##PRIVATE *)pThis->reserved

#define EMT_ROUNDTRIP(CUR, MIN, MAX) ((CUR) < (MIN) || (CUR) >= (MAX) ? (MIN) : (CUR))

static const uint32_t EMTPool_blockFromAddress(PEMTPOOL pThis, void * pMem)
{
	EMT_D(EMTPOOL);

	return (uint32_t)(((uint8_t *)pMem - (uint8_t *)d->pMem) / d->pMeta->uBlockLen);
}

static int32_t EMTPool_validation(PEMTPOOL pThis, void * pMem)
{
	EMT_D(EMTPOOL);
	const uint32_t uBlock = EMTPool_blockFromAddress(pThis, pMem);
	PEMTPOOLBLOCKMETA pBlockMeta = d->pBlockMeta + uBlock;

	if (uBlock < d->pMeta->uBlockStart || uBlock >= d->pMeta->uBlockCount)
		return kEMTPoolOutOfRange;

	//if (pBlockMeta->owner != d->uId)
	//	return kEMTPoolNotOwner;

	return kEMTPoolNoError;
}

static void EMTPool_construct(PEMTPOOL pThis, const uint32_t id, void * pMem, const uint32_t uMetaOffset, const uint32_t uMemLen, const uint32_t uBlockLen)
{
	EMT_D(EMTPOOL);

	pThis->id = EMTPool_id;
	pThis->address = EMTPool_address;
	pThis->memLength = EMTPool_memLength;
	pThis->blockLength = EMTPool_blockLength;
	pThis->length = EMTPool_length;
	pThis->alloc = EMTPool_alloc;
	pThis->free = EMTPool_free;
	pThis->transfer = EMTPool_transfer;
	pThis->take = EMTPool_take;

	d->uId = id;
	d->pMem = pMem;
	d->pMeta = (PEMTPOOLMETA)((char *)d->pMem + uMetaOffset);
	d->pBlockMeta = (PEMTPOOLBLOCKMETA)(d->pMeta + 1);

	EMTPool_init(pThis, uMemLen, uBlockLen);
}

static void EMTPool_destruct(PEMTPOOL pThis)
{
	EMT_D(EMTPOOL);

	EMTPool_freeAll(pThis, d->uId);
}

static void EMTPool_init(PEMTPOOL pThis, const uint32_t uMemLen, const uint32_t uBlockLen)
{
	EMT_D(EMTPOOL);

	const uint32_t uMagicNum = rt_cmpXchg32(&d->pMeta->uNextBlock, kEMTPoolMagicNumInit, kEMTPoolMagicNumUninit);
	while (uMagicNum != kEMTPoolMagicNumUninit && d->pMeta->uNextBlock == kEMTPoolMagicNumInit);

	if (uMagicNum != kEMTPoolMagicNumUninit)
		return;

	d->pMeta->uMemLen = uMemLen;
	d->pMeta->uBlockLen = uBlockLen;
	d->pMeta->uBlockCount = d->pMeta->uMemLen / d->pMeta->uBlockLen;
	d->pMeta->uBlockStart = (sizeof(*d->pMeta) + d->pMeta->uBlockCount * sizeof(*d->pBlockMeta) + uBlockLen - 1) / uBlockLen;

	rt_memset(d->pBlockMeta, 0, d->pMeta->uBlockCount * sizeof(*d->pBlockMeta));
	d->pBlockMeta[d->pMeta->uBlockStart].len = d->pMeta->uBlockCount - d->pMeta->uBlockStart;

	d->pMeta->uNextBlock = d->pMeta->uBlockStart;
}

static const uint32_t EMTPool_id(PEMTPOOL pThis)
{
	EMT_D(EMTPOOL);
	return d->uId;
}

static void * EMTPool_address(PEMTPOOL pThis)
{
	EMT_D(EMTPOOL);
	return d->pMem;
}

static const uint32_t EMTPool_memLength(PEMTPOOL pThis)
{
	EMT_D(EMTPOOL);
	return d->pMeta->uMemLen;
}

static const uint32_t EMTPool_blockLength(PEMTPOOL pThis)
{
	EMT_D(EMTPOOL);
	return d->pMeta->uBlockLen;
}

static const uint32_t EMTPool_length(PEMTPOOL pThis, void * pMem)
{
	EMT_D(EMTPOOL);
	const uint32_t uBlock = EMTPool_blockFromAddress(pThis, pMem);
	PEMTPOOLBLOCKMETA pBlockMeta = d->pBlockMeta + uBlock;
	return pBlockMeta->allocLen;
}

static void * EMTPool_alloc(PEMTPOOL pThis, const uint32_t uMemLen)
{
	EMT_D(EMTPOOL);

	const uint32_t uBlocks = (uMemLen + d->pMeta->uBlockLen - 1) / d->pMeta->uBlockLen;
	PEMTPOOLBLOCKMETA pBlockMeta = 0;
	while (pBlockMeta == 0 || pBlockMeta->len < uBlocks)
	{
		const uint32_t uBlockCurP = pBlockMeta ? pBlockMeta - d->pBlockMeta + pBlockMeta->len : d->pMeta->uNextBlock;
		const uint32_t uBlockCur = EMT_ROUNDTRIP(uBlockCurP, d->pMeta->uBlockStart, d->pMeta->uBlockCount);
		PEMTPOOLBLOCKMETA pBlockMetaCur = d->pBlockMeta + uBlockCur;
		const uint32_t uBlockNext = EMT_ROUNDTRIP(uBlockCur + pBlockMetaCur->len, d->pMeta->uBlockStart, d->pMeta->uBlockCount);

		const uint32_t bSuccess = rt_cmpXchg32(&d->pMeta->uNextBlock, uBlockNext, uBlockCur) == uBlockCur
			&& rt_cmpXchg32(&pBlockMetaCur->owner, d->uId, 0) == 0;

		if (pBlockMeta != 0 && (!bSuccess || uBlockCur != uBlockCurP))
		{
			pBlockMeta->owner = 0;
			pBlockMeta = 0;
		}

		if (bSuccess)
		{
			if (pBlockMeta != 0)
			{
				pBlockMeta->len += pBlockMetaCur->len;
			}
			else
			{
				pBlockMeta = pBlockMetaCur;
			}
		}
	}

	if (pBlockMeta->len > uBlocks)
	{
		PEMTPOOLBLOCKMETA pBlockMetaCur = pBlockMeta + uBlocks;
		pBlockMetaCur->len = pBlockMeta->len - uBlocks;
		pBlockMetaCur->owner = 0;
		pBlockMeta->len = uBlocks;
	}

	pBlockMeta->allocLen = uMemLen;
	return (uint8_t *)d->pMem + (pBlockMeta - d->pBlockMeta) * d->pMeta->uBlockLen;
}

static void EMTPool_free(PEMTPOOL pThis, void * pMem)
{
	EMT_D(EMTPOOL);
	const uint32_t uBlock = EMTPool_blockFromAddress(pThis, pMem);
	PEMTPOOLBLOCKMETA pBlockMeta = d->pBlockMeta + uBlock;

	if (EMTPool_validation(pThis, pMem) != kEMTPoolNoError)
		return;

	pBlockMeta->owner = 0;
}

void EMTPool_freeAll(PEMTPOOL pThis, const uint32_t uId)
{

}

static const uint32_t EMTPool_transfer(PEMTPOOL pThis, void * pMem, const uint32_t uToId)
{
	EMT_D(EMTPOOL);
	const uint32_t uBlock = EMTPool_blockFromAddress(pThis, pMem);
	PEMTPOOLBLOCKMETA pBlockMeta = d->pBlockMeta + uBlock;

	if (EMTPool_validation(pThis, pMem) != kEMTPoolNoError)
		return 0;

	pBlockMeta->owner = uToId;

	return uBlock;
}

static void * EMTPool_take(PEMTPOOL pThis, const uint32_t uToken)
{
	EMT_D(EMTPOOL);
	void * pMem = (uint8_t *)d->pMem + uToken * d->pMeta->uBlockLen;

	if (EMTPool_validation(pThis, pMem) != kEMTPoolNoError)
		return 0;

	return pMem;
}

void constructEMTPool(PEMTPOOL pEMTPool, const uint32_t id, void * pMem, const uint32_t uMetaOffset, const uint32_t uMemLen, const uint32_t uBlockLen)
{
	EMTPool_construct(pEMTPool, id, pMem, uMetaOffset, uMemLen, uBlockLen);
}

void destructEMTPool(PEMTPOOL pEMTPool)
{
	EMTPool_destruct(pEMTPool);
}
