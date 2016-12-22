#define EMTIMPL_CORE
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

	kEMTCoreSend = 0,
	kEMTCorePartial = 1,
	kEMTCoreConnect = 2,
	kEMTCoreDisconnect = 3,
	kEMTCoreTypeMask = (1 << 2) - 1,

	kEMTCoreFlagConnect = 1,
};

typedef struct _EMTCOREMEMMETA EMTCOREMEMMETA, * PEMTCOREMEMMETA;
struct _EMTCOREMEMMETA
{
	uint32_t uLen;
};

const EMTMULTIPOOLCONFIG sMultiPoolConfig[] =
{
	{ 32, 32 * 1024, 4 },
	{ 512, 2 * 1024 * 4, 4 },
	{ 1024 * 64, 16 * 16, 64 }
};

static int32_t EMTCore_isSharedMemory(PEMTCORE pThis, void * pMem)
{
	return pMem >= pThis->pMem && pMem < pThis->pMemEnd;
}

static void EMTCore_transfer(PEMTCORE pThis, void * pMem, const uint64_t uFlags, const uint64_t uParam0, const uint64_t uParam1, const uint32_t bNotify)
{
	PEMTCOREBLOCKMETA blockMeta = (PEMTCOREBLOCKMETA)EMTMultiPool_alloc(&pThis->sMultiPool, sizeof(EMTCOREBLOCKMETA));
	blockMeta->uToken = pMem ? EMTMultiPool_transfer(&pThis->sMultiPool, pMem, *pThis->pPeerIdR) : 0;
	blockMeta->uFlags = uFlags;
	blockMeta->uParam0 = uParam0;
	blockMeta->uParam1 = uParam1;
	EMTLinkList_prepend(pThis->pConnHeadR, &blockMeta->sNext);

	if (bNotify)
		pThis->pSinkOps->notify(pThis->pSinkCtx);
}

void EMTCore_construct(PEMTCORE pThis, PEMTCORESINKOPS pSinkOps, void * pSinkCtx)
{
	uint32_t metaLen, memLen;
	uint32_t i;
	pThis->pSinkOps = pSinkOps;
	pThis->pSinkCtx = pSinkCtx;
	pThis->uConnId = kEMTCoreInvalidConn;
	pThis->uFlags = 0;

	pThis->sMultiPool.uPoolCount = sizeof(pThis->sMultiPoolConfig) / sizeof(EMTMULTIPOOLCONFIG);
	for (i = 0; i < pThis->sMultiPool.uPoolCount; ++i)
		pThis->sMultiPoolConfig[i] = sMultiPoolConfig[i];

	EMTMultiPool_calcMetaSize(&pThis->sMultiPool, &metaLen, &memLen);

	metaLen = (metaLen + sizeof(EMTCOREMETA) + kPageMask) & ~kPageMask;

	void * mem = pThis->pSinkOps->getShareMemory(pThis->pSinkCtx, metaLen + memLen);

	pThis->pMem = mem;
	pThis->pMemEnd = (uint8_t *)mem + metaLen + memLen;

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
	return pThis->uFlags & kEMTCoreFlagConnect;
}

uint32_t EMTCore_connect(PEMTCORE pThis, uint32_t uConnId, const uint64_t uParam0, const uint64_t uParam1)
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

	EMTCore_transfer(pThis, 0, kEMTCoreConnect, uParam0, uParam1, 0);

	if (!isNewConn)
	{
		EMTCore_notified(pThis);
		pThis->pSinkOps->notify(pThis->pSinkCtx);
	}

	return pThis->uConnId;
}

uint32_t EMTCore_disconnect(PEMTCORE pThis)
{
	if (pThis->uConnId == kEMTCoreInvalidConn || *pThis->pPeerIdL == 0)
		return 0;

	*pThis->pPeerIdL = 0;
	EMTCore_transfer(pThis, 0, kEMTCoreDisconnect, 0, 0, 1);

	return 0;
}

void * EMTCore_alloc(PEMTCORE pThis, const uint32_t uLen)
{
	void * ret = EMTMultiPool_alloc(&pThis->sMultiPool, uLen);

	if (!ret)
	{
		PEMTCOREMEMMETA memMeta = (PEMTCOREMEMMETA)pThis->pSinkOps->allocSys(pThis->pSinkCtx, uLen);
		memMeta->uLen = uLen;
		ret = memMeta + 1;
	}

	return ret;
}

void EMTCore_free(PEMTCORE pThis, void * pMem)
{
	if (EMTCore_isSharedMemory(pThis, pMem))
		EMTMultiPool_free(&pThis->sMultiPool, pMem);
	else
		pThis->pSinkOps->freeSys(pThis->pSinkCtx, (PEMTCOREMEMMETA)pMem - 1);
}

void EMTCore_send(PEMTCORE pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1)
{
	if (EMTCore_isSharedMemory(pThis, pMem))
		EMTCore_transfer(pThis, pMem, kEMTCoreSend, uParam0, uParam1, 1);
}

void EMTCore_notified(PEMTCORE pThis)
{
	PEMTCOREBLOCKMETA blockMeta = (PEMTCOREBLOCKMETA)EMTLinkList_reverse(EMTLinkList_detach(pThis->pConnHeadL));

	while (blockMeta)
	{
		PEMTCOREBLOCKMETA curr = blockMeta;
		blockMeta = (PEMTCOREBLOCKMETA)EMTLinkList_next(&blockMeta->sNext);

		switch (curr->uFlags & kEMTCoreTypeMask)
		{
		case kEMTCoreSend:
			EMTCore_received(pThis, curr);
			break;
		case kEMTCorePartial:
			EMTCore_received(pThis, curr);
			break;
		case kEMTCoreConnect:
			EMTCore_connected(pThis, curr);
			break;
		case kEMTCoreDisconnect:
			EMTCore_disconnected(pThis, curr);
			break;
		}

		EMTMultiPool_free(&pThis->sMultiPool, curr);
	}
}

void EMTCore_queued(PEMTCORE pThis, void * pMem)
{
}

void EMTCore_connected(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta)
{
	pThis->pSinkOps->connected(pThis->pSinkCtx, pBlockMeta->uParam0, pBlockMeta->uParam1);
	pThis->uFlags |= kEMTCoreFlagConnect;
}

void EMTCore_disconnected(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta)
{
	pThis->uFlags &= ~kEMTCoreFlagConnect;
	pThis->pSinkOps->disconnected(pThis->pSinkCtx);
}

void EMTCore_received(PEMTCORE pThis, PEMTCOREBLOCKMETA pBlockMeta)
{
	void * mem = EMTMultiPool_take(&pThis->sMultiPool, pBlockMeta->uToken);
	pThis->pSinkOps->received(pThis->pSinkCtx, mem, pBlockMeta->uParam0, pBlockMeta->uParam1);
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
		EMTCore_send,
		EMTCore_notified,
		EMTCore_queued,
	};

	return &sOps;
}
