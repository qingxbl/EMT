/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTEXTEND_H__
#define __EMTEXTEND_H__

#include "EMTCore.h"

enum
{
	kEMTExtendSend = 1,
	kEMTExtendCall = 2,
	kEMTExtendResult = 3,

	kEMTExtendTypeMask = (1 << 2) - 1,
	kEMTExtendTypeOffset = sizeof(uint32_t),
};

static void EMTExtend_send(PEMTCORE pCore, void * pMem)
{
	emtCore()->send(pCore, pMem, kEMTExtendSend << kEMTExtendTypeOffset, 0);
}

static void EMTExtend_call(PEMTCORE pCore, void * pMem, const uint32_t uContext)
{
	emtCore()->send(pCore, pMem, (kEMTExtendCall << kEMTExtendTypeOffset) + uContext, 0);
}

static void EMTExtend_result(PEMTCORE pCore, void * pMem, const uint32_t uContext)
{
	emtCore()->send(pCore, pMem, (kEMTExtendResult << kEMTExtendTypeOffset) + uContext, 0);
}

static uint32_t EMTExtend_received(PEMTCORE pCore, void * pMem, const uint64_t uParam0, const uint64_t uParam1, uint32_t * pContext)
{
	const uint32_t type = (uint32_t)(uParam0 >> kEMTExtendTypeOffset);
	if ((type & kEMTExtendTypeMask) == type)
	{
		*pContext = (uint32_t)uParam0;
		return type;
	}

	return 0;
}

#endif // __EMTEXTEND_H__
