#define EMTIMPL_CORE
#include "EMTCore.h"
#include "EMTLinkList.h"

typedef struct _EMTCOREPARTIALMETA EMTCOREPARTIALMETA, * PEMTCOREPARTIALMETA;

#pragma pack(push, 1)
struct _EMTCOREMETA
{
	uint32_t dummy;
};

struct _EMTCORECONNMETA
{
	EMTLINKLISTNODE sHead[2];
	volatile uint32_t uPeerId[2];
};

struct _EMTCOREBLOCKMETA
{
	EMTLINKLISTNODE sNext;

	uint32_t uToken;
	uint64_t uFlags;
	uint64_t uParam0;
	uint64_t uParam1;
};

struct _EMTCOREPARTIALMETA
{
	uint64_t pSend;
	uint64_t pReceive;

	uint32_t uStart;

	uint32_t uTokenCount;
	uint32_t uToken[10];
};
#pragma pack(pop)

enum
{
	kPageShift = 12,
	kPageSize = 1 << kPageShift,
	kPageMask = kPageSize - 1,

	kEMTCoreSend = 0,
	kEMTCorePartial = 1,
	kEMTCoreTypeMask = (1 << 2) - 1,

	kEMTCoreLargestBlockLength = 1024 * 256,
	kEMTCoreLargestBlockCount = 4 * 4,
	kEMTCoreLargestBlockLimit = 4,
	KEMTCorePartialMemLength = kEMTCoreLargestBlockLength * kEMTCoreLargestBlockLimit,
	kEMTCorePartialSlots = 10,
};

typedef struct _EMTCOREMEMMETA EMTCOREMEMMETA, * PEMTCOREMEMMETA;
struct _EMTCOREMEMMETA
{
	uint32_t uLen;
};

const EMTMULTIPOOLCONFIG sMultiPoolConfig[] =
{
	{ 32, 32 * 1024, 4 },
	{ 4 * 1024, 256 * 6, 4 },
	{ kEMTCoreLargestBlockLength, kEMTCoreLargestBlockCount, kEMTCoreLargestBlockLimit }
};

static uint32_t EMTCore_process(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta);

static int32_t EMTCore_isSharedMemory(PEMTCORE pThis, void * pMem)
{
	return pMem >= pThis->pMem && pMem < pThis->pMemEnd;
}

static void EMTCore_sendAll(PEMTCORE pThis, void * pMem, const uint64_t uFlags, const uint64_t uParam0, const uint64_t uParam1)
{
	PEMTCOREBLOCKMETA blockMeta = (PEMTCOREBLOCKMETA)EMTMultiPool_alloc(&pThis->sMultiPool, sizeof(EMTCOREBLOCKMETA));
	blockMeta->uToken = pMem ? EMTMultiPool_transfer(&pThis->sMultiPool, pMem, *pThis->pPeerIdR) : 0;
	blockMeta->uFlags = uFlags;
	blockMeta->uParam0 = uParam0;
	blockMeta->uParam1 = uParam1;

	if (EMTLinkList_prepend(pThis->pConnHeadR, &blockMeta->sNext) == 0)
		pThis->pSinkOps->notify(pThis->pSinkCtx);
}

static void * EMTCore_allocSys(PEMTCORE pThis, const uint32_t uLen)
{
	PEMTCOREMEMMETA memMeta = (PEMTCOREMEMMETA)pThis->pSinkOps->allocSys(pThis->pSinkCtx, uLen + sizeof(EMTCOREMEMMETA));
	memMeta->uLen = uLen;
	return memMeta + 1;
}

static uint32_t EMTCore_pend(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta)
{
	if (pThis->pInTail == 0)
	{
		pThis->pInHead = pBlockMeta;
		pThis->pInTail = pBlockMeta;
		EMTLinkList_init(&pBlockMeta->sNext);
	}
	else if (pThis->pInHead != pBlockMeta)
	{
		EMTLinkList_prepend(&pThis->pInTail->sNext, &pBlockMeta->sNext);
		pThis->pInTail = pBlockMeta;
	}

	return pThis->pInHead == pBlockMeta ? 1 : 0;
}

static void EMTCore_popPended(PEMTCORE pThis)
{
	while (pThis->pInHead)
	{
		PEMTCOREBLOCKMETA curr = pThis->pInHead;

		if (EMTCore_process(pThis, curr) != 0)
		{
			pThis->pInHead = (PEMTCOREBLOCKMETA)EMTLinkList_next(&curr->sNext);
			EMTCore_free(pThis, curr);
		}
		else
			break;
	}

	if (!pThis->pInHead)
		pThis->pInTail = 0;
}

static uint32_t EMTCore_received(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta)
{
	if (pThis->pInHead == 0 || pThis->pInHead == pBlockMeta)
	{
		void * mem = EMTMultiPool_take(&pThis->sMultiPool, pBlockMeta->uToken);
		pThis->pSinkOps->received(pThis->pSinkCtx, mem, pBlockMeta->uParam0, pBlockMeta->uParam1);
		return 1;
	}
	else
	{
		return EMTCore_pend(pThis, pBlockMeta);
	}
}

static void EMTCore_sendPartialStart(PEMTCORE pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1)
{
	PEMTCOREPARTIALMETA partialMeta = EMTMultiPool_alloc(&pThis->sMultiPool, sizeof(EMTCOREPARTIALMETA));
	partialMeta->pSend = (uintptr_t)pMem;
	partialMeta->pReceive = 0;
	partialMeta->uStart = EMTCore_length(pThis, pMem);
	partialMeta->uTokenCount = 0;

	EMTCore_sendAll(pThis, partialMeta, kEMTCorePartial, uParam0, uParam1);
}

static uint32_t EMTCore_receivedPartialStart(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta, PEMTCOREPARTIALMETA pPartialMeta)
{
	pPartialMeta->pReceive = (uintptr_t)EMTCore_allocSys(pThis, pPartialMeta->uStart);
	pPartialMeta->uStart = 0;

	EMTCore_sendAll(pThis, pPartialMeta, kEMTCorePartial, 0, 0);
	return 0;
}

static uint32_t EMTCore_sendPartialData(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta, PEMTCOREPARTIALMETA pPartialMeta)
{
	uint8_t * mem = (uint8_t *)pPartialMeta->pSend;
	const uint32_t memLength = EMTCore_length(pThis, mem);
	uint32_t start = pPartialMeta->uStart;

	for (pPartialMeta->uTokenCount = 0; pPartialMeta->uTokenCount < kEMTCorePartialSlots && start < memLength; ++pPartialMeta->uTokenCount)
	{
		const uint32_t memRemain = memLength - start;
		const uint32_t memSendLength = memRemain > KEMTCorePartialMemLength ? KEMTCorePartialMemLength : memRemain;
		void * memSend = EMTMultiPool_alloc(&pThis->sMultiPool, memSendLength);

		if (memSend == 0)
			break;

		rt_memcpy(memSend, mem + start, memSendLength);
		start += memSendLength;
		pPartialMeta->uToken[pPartialMeta->uTokenCount] = EMTMultiPool_transfer(&pThis->sMultiPool, memSend, *pThis->pPeerIdR);
	}

	if (start == memLength)
		EMTCore_free(pThis, mem);

	if (pPartialMeta->uTokenCount != 0)
		EMTCore_sendAll(pThis, pPartialMeta, kEMTCorePartial, 0, 0);
	else
		pThis->pSinkOps->queue(pThis->pSinkCtx, pBlockMeta);

	return pPartialMeta->uTokenCount;
}

static uint32_t EMTCore_receivedPartialData(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta, PEMTCOREPARTIALMETA pPartialMeta)
{
	uint32_t i;
	uint8_t * mem = (uint8_t *)pPartialMeta->pReceive;
	const uint32_t memLen = EMTCore_length(pThis, mem);

	for (i = 0; i < pPartialMeta->uTokenCount; ++i)
	{
		void * memReceive = EMTMultiPool_take(&pThis->sMultiPool, pPartialMeta->uToken[i]);
		const uint32_t memReceiveLen = EMTMultiPool_length(&pThis->sMultiPool, memReceive);

		rt_memcpy(mem + pPartialMeta->uStart, memReceive, memReceiveLen);
		pPartialMeta->uStart += memReceiveLen;

		EMTMultiPool_free(&pThis->sMultiPool, memReceive);
	}

	if (pPartialMeta->uStart != memLen)
	{
		pPartialMeta->uTokenCount = 0;
		EMTCore_sendAll(pThis, pPartialMeta, kEMTCorePartial, 0, 0);
	}
	else
	{
		PEMTCOREBLOCKMETA realBlockMeta = pThis->pInHead;

		EMTMultiPool_free(&pThis->sMultiPool, pPartialMeta);
		pThis->pSinkOps->received(pThis->pSinkCtx, mem, realBlockMeta->uParam0, realBlockMeta->uParam1);

		// Loop thought pThis->pInHead;
		pThis->pInHead = (PEMTCOREBLOCKMETA)EMTLinkList_next(&realBlockMeta->sNext);
		EMTMultiPool_free(&pThis->sMultiPool, realBlockMeta);
		EMTCore_popPended(pThis);
	}

	return 1;
}

static uint32_t EMTCore_receivedPartial(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta)
{
	PEMTCOREPARTIALMETA partialMeta = (PEMTCOREPARTIALMETA)EMTMultiPool_take(&pThis->sMultiPool, pBlockMeta->uToken);

	if (partialMeta->pReceive == 0)
	{
		return EMTCore_pend(pThis, pBlockMeta) ? EMTCore_receivedPartialStart(pThis, pBlockMeta, partialMeta) : 0;
	}

	return partialMeta->uTokenCount != 0 ? EMTCore_receivedPartialData(pThis, pBlockMeta, partialMeta) : EMTCore_sendPartialData(pThis, pBlockMeta, partialMeta);
}

static uint32_t EMTCore_process(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta)
{
	switch (pBlockMeta->uFlags & kEMTCoreTypeMask)
	{
	case kEMTCoreSend:
		return EMTCore_received(pThis, pBlockMeta);
	case kEMTCorePartial:
		return EMTCore_receivedPartial(pThis, pBlockMeta);
	default:
		return 1;
	}
}

void EMTCore_construct(PEMTCORE pThis, PEMTCORESINKOPS pSinkOps, void * pSinkCtx)
{
	uint32_t metaLen, memLen;
	uint32_t i;
	pThis->pSinkOps = pSinkOps;
	pThis->pSinkCtx = pSinkCtx;
	pThis->uConnId = kEMTCoreInvalidConn;

	pThis->sMultiPool.uPoolCount = sizeof(pThis->sMultiPoolConfig) / sizeof(EMTMULTIPOOLCONFIG);
	for (i = 0; i < pThis->sMultiPool.uPoolCount; ++i)
		pThis->sMultiPoolConfig[i] = sMultiPoolConfig[i];

	EMTMultiPool_calcMetaSize(&pThis->sMultiPool, &metaLen, &memLen);

	metaLen = (metaLen + sizeof(EMTCOREMETA) + kPageMask) & ~kPageMask;

	void * mem = pThis->pSinkOps->getShareMemory(pThis->pSinkCtx, metaLen + memLen);

	pThis->pMem = mem;
	pThis->pMemEnd = (uint8_t *)mem + metaLen + memLen;
	pThis->pInHead = 0;
	pThis->pInTail = 0;

	pThis->pMeta = (PEMTCOREMETA)mem;
	mem = pThis->pMeta + 1;

	EMTMultiPool_construct(&pThis->sMultiPool, mem, (uint8_t *)pThis->pMem + metaLen);
}

void EMTCore_destruct(PEMTCORE pThis)
{
	if (pThis->uConnId != kEMTCoreInvalidConn && *pThis->pPeerIdL == 0 && *pThis->pPeerIdR == 0)
		EMTMultiPool_free(&pThis->sMultiPool, EMTMultiPool_take(&pThis->sMultiPool, pThis->uConnId));

	pThis->pSinkOps->releaseShareMemory(pThis->pSinkCtx, pThis->pMem);
}

uint32_t EMTCore_connId(PEMTCORE pThis)
{
	return pThis->uConnId;
}

uint32_t EMTCore_isConnected(PEMTCORE pThis)
{
	return pThis->pPeerIdL && pThis->pPeerIdR && *pThis->pPeerIdL && *pThis->pPeerIdR;
}

uint32_t EMTCore_connect(PEMTCORE pThis, uint32_t uConnId)
{
	const int32_t isNewConn = uConnId == kEMTCoreInvalidConn;
	PEMTCORECONNMETA connMeta = (PEMTCORECONNMETA)(isNewConn ? EMTMultiPool_alloc(&pThis->sMultiPool, sizeof(EMTCORECONNMETA)) : EMTMultiPool_take(&pThis->sMultiPool, uConnId));
	pThis->uConnId = isNewConn ? EMTMultiPool_transfer(&pThis->sMultiPool, connMeta, EMTMultiPool_id(&pThis->sMultiPool)) : uConnId;

	pThis->pConnHeadL = connMeta->sHead + (isNewConn ? 0 : 1);
	pThis->pConnHeadR = connMeta->sHead + (isNewConn ? 1 : 0);
	pThis->pPeerIdL = connMeta->uPeerId + (isNewConn ? 0 : 1);
	pThis->pPeerIdR = connMeta->uPeerId + (isNewConn ? 1 : 0);

	*pThis->pPeerIdL = EMTMultiPool_id(&pThis->sMultiPool);
	if (isNewConn)
	{
		*pThis->pPeerIdR = 0;
		EMTLinkList_init(pThis->pConnHeadL);
		EMTLinkList_init(pThis->pConnHeadR);
	}

	return pThis->uConnId;
}

uint32_t EMTCore_disconnect(PEMTCORE pThis)
{
	if (pThis->uConnId == kEMTCoreInvalidConn || *pThis->pPeerIdL == 0)
		return 0;

	*pThis->pPeerIdL = 0;

	return 0;
}

void * EMTCore_alloc(PEMTCORE pThis, const uint32_t uLen)
{
	void * ret = EMTMultiPool_alloc(&pThis->sMultiPool, uLen);

	return ret ? ret : EMTCore_allocSys(pThis, uLen);
}

void EMTCore_free(PEMTCORE pThis, void * pMem)
{
	if (EMTCore_isSharedMemory(pThis, pMem))
		EMTMultiPool_free(&pThis->sMultiPool, pMem);
	else if (pMem != 0)
		pThis->pSinkOps->freeSys(pThis->pSinkCtx, (PEMTCOREMEMMETA)pMem - 1);
}

uint32_t EMTCore_length(PEMTCORE pThis, void * pMem)
{
	if (EMTCore_isSharedMemory(pThis, pMem))
		return EMTMultiPool_length(&pThis->sMultiPool, pMem);
	else
		return ((PEMTCOREMEMMETA)pMem - 1)->uLen;
}

uint32_t EMTCore_transfer(PEMTCORE pThis, void * pMem)
{
	if (EMTCore_isSharedMemory(pThis, pMem))
		return EMTMultiPool_transfer(&pThis->sMultiPool, pMem, *pThis->pPeerIdR);
	else
		return kEMTCoreInvalidConn;
}

void * EMTCore_take(PEMTCORE pThis, const uint32_t uToken)
{
	if (uToken != kEMTCoreInvalidConn)
		return EMTMultiPool_take(&pThis->sMultiPool, uToken);
	else
		return 0;
}

void EMTCore_send(PEMTCORE pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1)
{
	if (EMTCore_isSharedMemory(pThis, pMem))
		EMTCore_sendAll(pThis, pMem, kEMTCoreSend, uParam0, uParam1);
	else
		EMTCore_sendPartialStart(pThis, pMem, uParam0, uParam1);
}

void EMTCore_notified(PEMTCORE pThis)
{
	PEMTCOREBLOCKMETA blockMeta = (PEMTCOREBLOCKMETA)EMTLinkList_reverse(EMTLinkList_detach(pThis->pConnHeadL));

	while (blockMeta)
	{
		PEMTCOREBLOCKMETA curr = blockMeta;
		blockMeta = (PEMTCOREBLOCKMETA)EMTLinkList_next(&blockMeta->sNext);

		if (EMTCore_process(pThis, curr) != 0)
			EMTMultiPool_free(&pThis->sMultiPool, curr);
	}
}

void EMTCore_queued(PEMTCORE pThis, void * pMem)
{
	if (EMTCore_process(pThis, (PEMTCOREBLOCKMETA)pMem) != 0)
		EMTMultiPool_free(&pThis->sMultiPool, pMem);
}

PCEMTCOREOPS emtCore(void)
{
	static const EMTCOREOPS sOps =
	{
		EMTCore_construct,
		EMTCore_destruct,
		EMTCore_connId,
		EMTCore_isConnected,
		EMTCore_connect,
		EMTCore_disconnect,
		EMTCore_alloc,
		EMTCore_free,
		EMTCore_length,
		EMTCore_transfer,
		EMTCore_take,
		EMTCore_send,
		EMTCore_notified,
		EMTCore_queued,
	};

	return &sOps;
}
