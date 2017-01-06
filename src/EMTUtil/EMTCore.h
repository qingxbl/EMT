/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTCORE_H__
#define __EMTCORE_H__

#include "EMTMultiPool.h"
#include "EMTLinkList.h"

enum
{
	kEMTCoreInvalidConn = ~0U,
};

typedef struct _EMTCOREOPS EMTCOREOPS, * PEMTCOREOPS;
typedef const EMTCOREOPS * PCEMTCOREOPS;
typedef struct _EMTCORE EMTCORE, * PEMTCORE;
typedef struct _EMTCORESINKOPS EMTCORESINKOPS, * PEMTCORESINKOPS;
typedef struct _EMTCOREMETA EMTCOREMETA, * PEMTCOREMETA;
typedef struct _EMTCORECONNMETA EMTCORECONNMETA, *PEMTCORECONNMETA;
typedef struct _EMTCOREBLOCKMETA EMTCOREBLOCKMETA, * PEMTCOREBLOCKMETA;

struct _EMTCOREOPS
{
	void (*construct)(PEMTCORE pThis, PEMTCORESINKOPS pSink, void * pSinkCtx);
	void (*destruct)(PEMTCORE pThis);

	uint32_t (*connId)(PEMTCORE pThis);
	uint32_t (*isConnected)(PEMTCORE pThis);

	uint32_t (*connect)(PEMTCORE pThis, uint32_t uConnId, const uint64_t uParam0, const uint64_t uParam1);
	uint32_t (*disconnect)(PEMTCORE pThis);

	void * (*alloc)(PEMTCORE pThis, const uint32_t uLen);
	void (*free)(PEMTCORE pThis, void * pMem);
	uint32_t (*length)(PEMTCORE pThis, void * pMem);


	void (*send)(PEMTCORE pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1);

	/* callback */
	void (*notified)(PEMTCORE pThis);
	void (*queued)(PEMTCORE pThis, void * pMem);
};

struct _EMTCORESINKOPS
{
	/* callback */
	void (*connected)(void * pThis, const uint64_t uParam0, const uint64_t uParam1);
	void (*disconnected)(void * pThis);

	void (*received)(void * pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1);

	/* support */
	void * (*getShareMemory)(void * pThis, const uint32_t uLen);
	void (*releaseShareMemory)(void * pThis, void * pMem);

	void (*notify)(void * pThis);
	void (*queue)(void * pThis, void * pMem);

	/* fallback */
	void * (*allocSys)(void * pThis, const uint32_t uLen);
	void (*freeSys)(void * pThis, void * pMem);
};

struct _EMTCORE
{
	/* Private fields */
	PEMTCORESINKOPS pSinkOps;
	void * pSinkCtx;

	PEMTCOREMETA pMeta;

	PEMTLINKLISTNODE pConnHeadL;
	PEMTLINKLISTNODE pConnHeadR;
	volatile uint32_t * pPeerIdL;
	volatile uint32_t * pPeerIdR;
	uint32_t uConnId;
	uint32_t uFlags;

	void * pMem;
	void * pMemEnd;

	PEMTCOREBLOCKMETA pInHead;
	PEMTCOREBLOCKMETA pInTail;

	EMTMULTIPOOL sMultiPool;
	EMTMULTIPOOLCONFIG sMultiPoolConfig[3];
};

EXTERN_C PCEMTCOREOPS emtCore(void);

#if !defined(USE_VTABLE) || defined(EMTIMPL_CORE)
EMTIMPL_CALL void EMTCore_construct(PEMTCORE pThis, PEMTCORESINKOPS pSinkOps, void * pSinkCtx);
EMTIMPL_CALL void EMTCore_destruct(PEMTCORE pThis);
EMTIMPL_CALL uint32_t EMTCore_connId(PEMTCORE pThis);
EMTIMPL_CALL uint32_t EMTCore_isConnected(PEMTCORE pThis);
EMTIMPL_CALL uint32_t EMTCore_connect(PEMTCORE pThis, uint32_t uConnId, const uint64_t uParam0, const uint64_t uParam1);
EMTIMPL_CALL uint32_t EMTCore_disconnect(PEMTCORE pThis);
EMTIMPL_CALL void * EMTCore_alloc(PEMTCORE pThis, const uint32_t uLen);
EMTIMPL_CALL void EMTCore_free(PEMTCORE pThis, void * pMem);
EMTIMPL_CALL uint32_t EMTCore_length(PEMTCORE pThis, void * pMem);
EMTIMPL_CALL void EMTCore_send(PEMTCORE pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1);
EMTIMPL_CALL void EMTCore_notified(PEMTCORE pThis);
EMTIMPL_CALL void EMTCore_queued(PEMTCORE pThis, void * pMem);
#else
#define EMTCore_construct emtCore()->construct
#define EMTCore_destruct emtCore()->destruct
#define EMTCore_connId emtCore()->connId
#define EMTCore_isConnected emtCore()->isConnected
#define EMTCore_connect emtCore()->connect
#define EMTCore_disconnect emtCore()->disconnect
#define EMTCore_alloc emtCore()->alloc
#define EMTCore_free emtCore()->free
#define EMTCore_length emtCore()->length
#define EMTCore_send emtCore()->send
#define EMTCore_notified emtCore()->notified
#define EMTCore_queued emtCore()->queued
#endif

EXTERN_C void * rt_memcpy(void * dst, const void * src, const uint32_t size);

#endif // __EMTCORE_H__
