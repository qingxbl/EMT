/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTIPCPRIVATE_H__
#define __EMTIPCPRIVATE_H__

#include <EMTUtil/EMTCore.h>

struct IEMTThread;
struct IEMTShareMemory;
struct IEMTIPCSink;

class EMTIPCPrivate
{
public:
	EMTIPCPrivate();
	virtual ~EMTIPCPrivate();

	void init(IEMTThread * pThread, IEMTShareMemory * pShareMemory, IEMTIPCSink * pSink);

	void notified();

private: // EMTCORESINKOPS
	static void connected(EMTIPCPrivate * pThis, const uint64_t uParam0, const uint64_t uParam1);
	static void disconnected(EMTIPCPrivate * pThis);
	static void received(EMTIPCPrivate * pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1);
	static void * getShareMemory(EMTIPCPrivate * pThis, const uint32_t uLen);
	static void releaseShareMemory(EMTIPCPrivate * pThis, void * pMem);
	static void notify(EMTIPCPrivate * pThis);
	static void queue(EMTIPCPrivate * pThis, void * pMem);
	static void * allocSys(EMTIPCPrivate * pThis, const uint32_t uLen);
	static void freeSys(EMTIPCPrivate * pThis, void * pMem);

	static PEMTCORESINKOPS emtCoreSink();

private:
	uint32_t sys_connect(uint32_t uConnId);

	void sys_connected(const uint64_t uParam0, const uint64_t uParam1);
	void sys_disconnected();
	void sys_notify();

protected:
	friend class EMTIPC;
	EMTIPC * q_ptr;

	IEMTThread * mThread;
	IEMTShareMemory * mShareMemory;
	IEMTIPCSink * mSink;
	EMTCORE mCore;
};

#endif // __EMTIPCPRIVATE_H__
