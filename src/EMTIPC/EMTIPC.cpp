#include "EMTIPC.h"

#include "EMTIPCPrivate.h"

#include <EMTUtil/EMTExtend.h>
#include <EMTUtil/EMTThread.h>
#include <EMTUtil/EMTShareMemory.h>

EMTIPCPrivate::EMTIPCPrivate()
{
}

EMTIPCPrivate::~EMTIPCPrivate()
{
	emtCore()->destruct(&mCore);

	mShareMemory->destruct();
}

void EMTIPCPrivate::init(IEMTThread * pThread, IEMTShareMemory * pShareMemory, IEMTIPCSink * pSink)
{
	mThread = pThread;
	mShareMemory = pShareMemory;
	mSink = pSink;

	emtCore()->construct(&mCore, emtCoreSink(), this);
}

void EMTIPCPrivate::notified()
{
	emtCore()->notified(&mCore);
}

void EMTIPCPrivate::connected(EMTIPCPrivate * pThis, const uint64_t uParam0, const uint64_t uParam1)
{
	pThis->sys_connected(uParam0, uParam1);

	pThis->mSink->connected();
}

void EMTIPCPrivate::disconnected(EMTIPCPrivate * pThis)
{
	pThis->sys_disconnected();

	pThis->mSink->disconnected();
}

void EMTIPCPrivate::received(EMTIPCPrivate * pThis, void * pMem, const uint64_t uParam0, const uint64_t uParam1)
{
	uint32_t context;
	switch (EMTExtend_received(&pThis->mCore, pMem, uParam0, uParam1, &context))
	{
	case kEMTExtendSend:
		pThis->mSink->received(pMem);
		break;
	case kEMTExtendCall:
		pThis->mSink->called(pMem, context);
		break;
	case kEMTExtendResult:
		pThis->mSink->resulted(pMem, context);
		break;
	default:
		break;
	}
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
	pThis->mThread->queue(createEMTRunnable(std::bind(emtCore()->queued, &pThis->mCore, pMem)));
}

void * EMTIPCPrivate::allocSys(EMTIPCPrivate * pThis, const uint32_t uLen)
{
	return ::malloc(uLen);
}

void EMTIPCPrivate::freeSys(EMTIPCPrivate * pThis, void * pMem)
{
	::free(pMem);
}

PEMTCORESINKOPS EMTIPCPrivate::emtCoreSink()
{
	static EMTCORESINKOPS sOps =
	{
		(void (*)(void * pThis, const uint64_t uParam0, const uint64_t uParam1))connected,
		(void (*)(void * pThis))disconnected,
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
	return emtCore()->connId(&d->mCore);
}

bool EMTIPC::isConnected()
{
	EMT_D(EMTIPC);
	return emtCore()->isConnected(&d->mCore) != 0;
}

uint32_t EMTIPC::connect(uint32_t uConnId)
{
	EMT_D(EMTIPC);
	return d->sys_connect(uConnId);
}

uint32_t EMTIPC::disconnect()
{
	EMT_D(EMTIPC);
	return emtCore()->disconnect(&d->mCore);
}

void * EMTIPC::alloc(const uint32_t uLen)
{
	EMT_D(EMTIPC);
	return emtCore()->alloc(&d->mCore, uLen);
}

void EMTIPC::free(void * pMem)
{
	EMT_D(EMTIPC);
	return emtCore()->free(&d->mCore, pMem);
}

void EMTIPC::send(void * pMem)
{
	EMT_D(EMTIPC);
	EMTExtend_send(&d->mCore, pMem);
}

void EMTIPC::call(void * pMem, const uint32_t uContext)
{
	EMT_D(EMTIPC);
	EMTExtend_call(&d->mCore, pMem, uContext);
}

void EMTIPC::result(void * pMem, const uint32_t uContext)
{
	EMT_D(EMTIPC);
	EMTExtend_result(&d->mCore, pMem, uContext);
}
