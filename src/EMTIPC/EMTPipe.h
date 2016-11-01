/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTPIPE_H__
#define __EMTPIPE_H__

#include <EMTCommon.h>

enum
{
	kEMTPipeDefaultTimeout = 5000,
	kEMTPipeBufferSize = 4096,
};

struct IEMTThread;

struct DECLSPEC_NOVTABLE IEMTPipeHandler : public IEMTUnknown
{
	virtual void connected() = 0;
	virtual void disconnected() = 0;

	virtual void received(void * buf) = 0;
	virtual void sent(void * buf, const uint32_t len) = 0;
};

struct DECLSPEC_NOVTABLE IEMTPipe : public IEMTUnknown
{
	virtual bool listen(wchar_t * name) = 0;
	virtual bool connect(wchar_t * name) = 0;
	virtual void close() = 0;

	virtual void send(void * buf, const uint32_t len) = 0;
};

IEMTPipe * createEMTPipe(IEMTThread * thread, IEMTPipeHandler * pipeHandler, const uint32_t bufferSize = kEMTPipeBufferSize, const uint32_t timeout = kEMTPipeDefaultTimeout);

#endif // __EMTPIPE_H__
