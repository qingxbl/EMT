/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTIPC_H__
#define __EMTIPC_H__

#include <EMTCommon.h>

struct IEMTThread;

struct DECLSPEC_NOVTABLE IEMTIPCHandler : public IEMTUnknown
{
	virtual void connected() = 0;
	virtual void disconnected() = 0;

	virtual void received(void * buf) = 0;
	virtual void sent(void * buf) = 0;
};

struct DECLSPEC_NOVTABLE IEMTIPC : public IEMTUnknown
{
	virtual bool isConnected() = 0;

	virtual bool listen(const wchar_t *name) = 0;
	virtual bool connect(const wchar_t *name) = 0;
	virtual void disconnect() = 0;

	virtual void * alloc(const uint32_t len) = 0;
	virtual void free(void * buf) = 0;

	virtual void send(void * buf) = 0;
};

IEMTIPC * createEMTIPC(IEMTThread * thread, IEMTIPCHandler * handler);

#endif // __EMTIPC_H__
