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
	uint32_t (*construct)(PEMTCORE pThis, PEMTCORESINKOPS pSink, void * pSinkCtx);
	void (*destruct)(PEMTCORE pThis);

	uint32_t (*connId)(PEMTCORE pThis);

	uint32_t (*connect)(PEMTCORE pThis, uint32_t uConnId, const uint64_t uParam0, const uint64_t uParam1);
	uint32_t (*disconnect)(PEMTCORE pThis);

	void * (*alloc)(PEMTCORE pThis, const uint32_t uLen);
	void (*free)(PEMTCORE pThis, void * pMem);

	void (*send)(PEMTCORE pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1);

	/* callback */
	void (*notified)(PEMTCORE pThis);
};

struct _EMTCORESINKOPS
{
	/* callback */
	void (*connected)(void * pThis, const uint64_t uParam0, const uint64_t uParam1);
	void (*disconnected)(void * pThis);

	void (*received)(void * pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1);

	/* support */
	void * (*getShareMemory)(void * pThis, uint32_t uLen);
	void (*releaseShareMemory)(void * pThis, void * pMem);

	void (*notify)(void * pThis);

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

	void * pMem;
	void * pMemEnd;

	EMTMULTIPOOL sMultiPool;
	EMTMULTIPOOLCONFIG sMultiPoolConfig[2];
};

EXTERN_C PCEMTCOREOPS emtCore(void);

#endif // __EMTCORE_H__
