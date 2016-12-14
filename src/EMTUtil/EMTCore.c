#include "EMTCore.h"
#include "EMTLinkList.h"

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
#pragma pack(pop)

enum
{
	kPageShift = 12,
	kPageSize = 1 << kPageShift,
	kPageMask = kPageSize - 1,

	kEMTCoreConnMax = 256,

	kEMTCoreSend = 0,
	kEMTCorePartial = 1,
	kEMTCoreConnect = 2,
	kEMTCoreDisconnect = 3,
	kEMTCoreTypeMask = (1 << 2) - 1,
};

static const EMTMULTIPOOLCONFIG sMultiPoolConfig[] =
{
	{ 32, 4 * 1024, 4 },
	{ 512, 2 * 512, 4 },
	{ 1024 * 64, 16 * 16, 64 }
};

static uint32_t EMTCore_construct(PEMTCORE pThis, PEMTCORESINKOPS pSinkOps, void * pSinkCtx);
static void EMTCore_destruct(PEMTCORE pThis);
static uint32_t EMTCore_connId(PEMTCORE pThis);
static uint32_t EMTCore_connect(PEMTCORE pThis, uint32_t uConnId, const uint64_t uParam0, const uint64_t uParam1);
static uint32_t EMTCore_disconnect(PEMTCORE pThis);
static void * EMTCore_alloc(PEMTCORE pThis, const uint32_t uLen);
static void EMTCore_free(PEMTCORE pThis, void * pMem);
static void EMTCore_send(PEMTCORE pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1);
static void EMTCore_notified(PEMTCORE pThis);

static void EMTCore_connected(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta);
static void EMTCore_disconnected(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta);
static void EMTCore_received(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta);

static int32_t EMTCore_isSharedMemory(PEMTCORE pThis, void * pMem)
{
	return pMem >= pThis->pMem && pMem < pThis->pMemEnd;
}

static void EMTCore_transfer(PEMTCORE pThis, void * pMem, const uint64_t uFlags, const uint64_t uParam0, const uint64_t uParam1, const uint32_t bNotify)
{
	PEMTCOREBLOCKMETA blockMeta = (PEMTCOREBLOCKMETA)emtMultiPool()->alloc(&pThis->sMultiPool, sizeof(EMTCOREBLOCKMETA));
	blockMeta->uToken = pMem ? emtMultiPool()->transfer(&pThis->sMultiPool, pMem, *pThis->pPeerIdR) : 0;
	blockMeta->uFlags = uFlags;
	blockMeta->uParam0 = uParam0;
	blockMeta->uParam1 = uParam1;
	emtLinkList()->prepend(pThis->pConnHeadR, &blockMeta->sNext);

	if (bNotify)
		pThis->pSinkOps->notify(pThis->pSinkCtx);
}

static uint32_t EMTCore_construct(PEMTCORE pThis, PEMTCORESINKOPS pSinkOps, void * pSinkCtx)
{
	uint32_t metaLen, memLen;
	uint32_t i;
	pThis->pSinkOps = pSinkOps;
	pThis->pSinkCtx = pSinkCtx;
	pThis->uConnId = kEMTCoreInvalidConn;

	pThis->sMultiPool.uPoolCount = sizeof(pThis->sMultiPoolConfig) / sizeof(EMTMULTIPOOLCONFIG);
	for (i = 0; i < pThis->sMultiPool.uPoolCount; ++i)
		pThis->sMultiPoolConfig[i] = sMultiPoolConfig[i];

	emtMultiPool()->calcMetaSize(&pThis->sMultiPool, &metaLen, &memLen);

	metaLen = (metaLen + sizeof(EMTCOREMETA) + kPageMask) & ~kPageMask;

	void * mem = pThis->pSinkOps->getShareMemory(pThis->pSinkCtx, metaLen + memLen);

	pThis->pMem = mem;
	pThis->pMemEnd = (uint8_t *)mem + metaLen + memLen;

	pThis->pMeta = (PEMTCOREMETA)mem;
	mem = pThis->pMeta + 1;

	emtMultiPool()->construct(&pThis->sMultiPool, mem, (uint8_t *)pThis->pMem + metaLen);
}

static void EMTCore_destruct(PEMTCORE pThis)
{
	pThis->pSinkOps->releaseShareMemory(pThis->pSinkCtx, pThis->pMem);
}

static uint32_t EMTCore_connId(PEMTCORE pThis)
{
	return pThis->uConnId;
}

static uint32_t EMTCore_connect(PEMTCORE pThis, uint32_t uConnId, const uint64_t uParam0, const uint64_t uParam1)
{
	const int32_t isNewConn = uConnId == kEMTCoreInvalidConn;
	PEMTCORECONNMETA connMeta = (PEMTCORECONNMETA)(isNewConn ? emtMultiPool()->alloc(&pThis->sMultiPool, sizeof(EMTCORECONNMETA)) : emtMultiPool()->take(&pThis->sMultiPool, uConnId));
	pThis->uConnId = isNewConn ? emtMultiPool()->transfer(&pThis->sMultiPool, connMeta, emtMultiPool()->id(&pThis->sMultiPool)) : uConnId;

	pThis->pConnHeadL = connMeta->sHead + (isNewConn ? 0 : 1);
	pThis->pConnHeadR = connMeta->sHead + (isNewConn ? 1 : 0);
	pThis->pPeerIdL = connMeta->uPeerId + (isNewConn ? 0 : 1);
	pThis->pPeerIdR = connMeta->uPeerId + (isNewConn ? 1 : 0);

	if (!isNewConn)
		EMTCore_notified(pThis);

	EMTCore_transfer(pThis, 0, kEMTCoreConnect, uParam0, uParam1, !isNewConn);
}

static uint32_t EMTCore_disconnect(PEMTCORE pThis)
{
	if (pThis->uConnId == kEMTCoreInvalidConn)
	{
		EMTCore_transfer(pThis, 0, kEMTCoreDisconnect, 0, 0, 1);
	}
}

static void * EMTCore_alloc(PEMTCORE pThis, const uint32_t uLen)
{
	void * ret = emtMultiPool()->alloc(&pThis->sMultiPool, uLen);

	return ret ? ret : pThis->pSinkOps->allocSys(pThis->pSinkCtx, uLen);
}

static void EMTCore_free(PEMTCORE pThis, void * pMem)
{
	if (EMTCore_isSharedMemory(pThis, pMem))
		emtMultiPool()->free(&pThis->sMultiPool, pMem);
	else
		pThis->pSinkOps->freeSys(pThis->pSinkCtx, pMem);
}

static void EMTCore_send(PEMTCORE pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1)
{
	if (EMTCore_isSharedMemory(pThis, pMem))
		EMTCore_transfer(pThis, pMem, kEMTCoreSend, uParam0, uParam1, 1);
}

static void EMTCore_notified(PEMTCORE pThis)
{
	PEMTCOREBLOCKMETA blockMeta = (PEMTCOREBLOCKMETA)emtLinkList()->reverse(emtLinkList()->detach(pThis->pConnHeadL));

	while (blockMeta)
	{
		PEMTCOREBLOCKMETA curr = blockMeta;
		blockMeta = (PEMTCOREBLOCKMETA)emtLinkList()->next(&blockMeta->sNext);

		switch (blockMeta->uFlags & kEMTCoreTypeMask)
		{
		case kEMTCoreSend:
			EMTCore_received(pThis, blockMeta);
			break;
		case kEMTCorePartial:
			EMTCore_received(pThis, blockMeta);
			break;
		case kEMTCoreConnect:
			EMTCore_connected(pThis, blockMeta);
			break;
		case kEMTCoreDisconnect:
			EMTCore_disconnected(pThis, blockMeta);
			break;
		}

		emtMultiPool()->free(&pThis->sMultiPool, curr);
	}
}

static void EMTCore_connected(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta)
{
	pThis->pSinkOps->connected(pThis->pSinkCtx, pBlockMeta->uParam0, pBlockMeta->uParam1);
}

static void EMTCore_disconnected(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta)
{
	pThis->pSinkOps->disconnected(pThis->pSinkCtx);
}

static void EMTCore_received(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta)
{
	void * mem = emtMultiPool()->take(&pThis->sMultiPool, pBlockMeta->uToken);
	pThis->pSinkOps->received(pThis->pSinkCtx, mem, pBlockMeta->uParam0, pBlockMeta->uParam1);
}

PCEMTCOREOPS emtCore(void)
{
	static const EMTCOREOPS sOps =
	{
		EMTCore_construct,
		EMTCore_destruct,
		EMTCore_connId,
		EMTCore_connect,
		EMTCore_disconnect,
		EMTCore_alloc,
		EMTCore_free,
		EMTCore_send,
		EMTCore_notified,
	};

	return &sOps;
}
