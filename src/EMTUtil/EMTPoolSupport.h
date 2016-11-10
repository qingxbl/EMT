/*
* EMT - Enhanced Memory Transfer (not emiria-tan)
*/

#ifndef __EMTPOOLSUPPORT_H__
#define __EMTPOOLSUPPORT_H__

#include <EMTCommon.h>

struct DECLSPEC_NOVTABLE IEMTMultiPool : public IEMTUnknown
{
	virtual void addConfig(const uint32_t length, const uint32_t blockLength, const uint32_t blockLimit) = 0;
	virtual void init(void * p) = 0;

	virtual uint32_t id() = 0;

	virtual void * alloc(const uint32_t memLength) = 0;
	virtual void free(void * mem) = 0;
	virtual void freeAll(const uint32_t id) = 0;

	virtual const bool transfer(void * mem, const uint32_t toId, uint32_t * token, uint32_t * poolId) = 0;
	virtual void transferPartial(void * mem, const uint32_t toId, uint32_t * token, uint32_t * poolId) = 0;
	virtual void * take(const uint32_t token, const uint32_t poolId) = 0;
};

IEMTMultiPool * createEMTMultiPool();

#endif // __EMTPOOLSUPPORT_H__
