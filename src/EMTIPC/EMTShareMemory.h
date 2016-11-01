/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTSHAREMEMORY_H__
#define __EMTSHAREMEMORY_H__

#include <EMTCommon.h>

struct DECLSPEC_NOVTABLE IEMTShareMemory : public IEMTUnknown
{
	virtual uint32_t length() = 0;
	virtual void * address() = 0;

	virtual void * open(wchar_t * name, const uint32_t length) = 0;
	virtual void close() = 0;
};

IEMTShareMemory * createEMTShareMemory();

#endif // __EMTSHAREMEMORY_H__
