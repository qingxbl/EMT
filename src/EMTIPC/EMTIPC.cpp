#include "EMTIPC.h"

#include "EMTIPCPrivate.h"

#include <EMTUtil/EMTThread.h>
#include <EMTUtil/EMTShareMemory.h>

EMTIPCPrivate::EMTIPCPrivate()
{
}

EMTIPCPrivate::~EMTIPCPrivate()
{
	EMTCore_destruct(&mCore);

	mShareMemory->destruct();
}

void EMTIPCPrivate::init(IEMTThread * pThread, IEMTShareMemory * pShareMemory, IEMTIPCSink * pSink)
{
	mThread = pThread;
	mShareMemory = pShareMemory;
	mSink = pSink;

	EMTCore_construct(&mCore, emtCoreSink(), this);
}

void EMTIPCPrivate::notified()
{
	EMTCore_notified(&mCore);
}

void EMTIPCPrivate::connected()
{
	mSink->connected();
}

void EMTIPCPrivate::disconnected()
{
	mSink->disconnected();
}

void EMTIPCPrivate::received(EMTIPCPrivate * pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1)
{
	pThis->mSink->received(pMem, uParam0, uParam1);
}

void * EMTIPCPrivate::getShareMemory(EMTIPCPrivate * pThis, uint32_t uLen)
{
	return pThis->mShareMemory->open(uLen);
}

void EMTIPCPrivate::releaseShareMemory(EMTIPCPrivate * pThis, void * pMem)
{
	pThis->mShareMemory->close();
}

void EMTIPCPrivate::notify(EMTIPCPrivate * pThis)
{
	pThis->sys_notify();
}

void EMTIPCPrivate::queue(EMTIPCPrivate * pThis, void * pMem)
{
	pThis->mThread->queue(createEMTRunnable(std::bind(EMTCore_queued, &pThis->mCore, pMem)));
}

PEMTCORESINKOPS EMTIPCPrivate::emtCoreSink()
{
	static EMTCORESINKOPS sOps =
	{
		(void (*)(void * pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1))received,
		(void * (*)(void * pThis, const uint32_t uLen))getShareMemory,
		(void (*)(void * pThis, void * pMem))releaseShareMemory,
		(void (*)(void * pThis))notify,
		(void (*)(void * pThis, void * pMem))queue,
		(void * (*)(void * pThis, const uint32_t uLen))allocSys,
		(void (*)(void * pThis, void * pMem))freeSys,
	};

	return &sOps;
}

EMTIPC::EMTIPC(EMTIPCPrivate & dd, IEMTThread * pThread, IEMTShareMemory * pShareMemory, IEMTIPCSink * pSink)
	: d_ptr(&dd)
{
	EMT_D(EMTIPC);

	d->q_ptr = this;
	d->init(pThread, pShareMemory, pSink);
}

EMTIPC::~EMTIPC()
{
}

IEMTThread * EMTIPC::thread() const
{
	EMT_D(EMTIPC);
	return d->mThread;
}

IEMTShareMemory * EMTIPC::shareMemory() const
{
	EMT_D(EMTIPC);
	return d->mShareMemory;
}

IEMTIPCSink * EMTIPC::sink() const
{
	EMT_D(EMTIPC);
	return d->mSink;
}

uint32_t EMTIPC::connId()
{
	EMT_D(EMTIPC);
	return EMTCore_connId(&d->mCore);
}

bool EMTIPC::isConnected()
{
	EMT_D(EMTIPC);
	return EMTCore_isConnected(&d->mCore) != 0;
}

void * EMTIPC::alloc(const uint32_t uLen)
{
	EMT_D(EMTIPC);
	return EMTCore_alloc(&d->mCore, uLen);
}

void EMTIPC::free(void * pMem)
{
	EMT_D(EMTIPC);
	return EMTCore_free(&d->mCore, pMem);
}

uint32_t EMTIPC::length(void * pMem)
{
	EMT_D(EMTIPC);
	return EMTCore_length(&d->mCore, pMem);
}

uint32_t EMTIPC::transfer(void * pMem)
{
	EMT_D(EMTIPC);
	return EMTCore_transfer(&d->mCore, pMem);
}

void * EMTIPC::take(const uint32_t uToken)
{
	EMT_D(EMTIPC);
	return EMTCore_take(&d->mCore, uToken);
}

void EMTIPC::send(void * pMem, const uint64_t uParam0, const uint64_t uParam1)
{
	EMT_D(EMTIPC);
	EMTCore_send(&d->mCore, pMem, uParam0, uParam1);
}
