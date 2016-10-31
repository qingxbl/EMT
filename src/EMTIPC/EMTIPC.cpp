#include "EMTIPC.h"

#include "EMTPipe.h"
#include "EMTThread.h"

#include <EMTPool/EMTPool.h>

#include <Windows.h>

BEGIN_NAMESPACE_ANONYMOUS

enum
{
	kDefaultTimeout = 5000,
	kBufferSize = 512,
};

enum
{
	kEMTPipeProtoBase = 0,
	kEMTPipeProtoHandshake,
	kEMTPipeProtoTransfer,
};

struct EMTPipeProtoBase
{
	uint16_t uri;
	uint16_t len;
};

template <uint16_t URI, class T>
struct EMTPipeProto : EMTPipeProtoBase
{
	typedef T FinalT;
	enum { kUri = URI };

	EMTPipeProto() : uri(kUri), len(sizeof(FinalT)) { }

	uint16_t uri;
	uint16_t len;
};

struct EMTPipeProtoHandshake : public EMTPipeProto<kEMTPipeProtoHandshake, EMTPipeProtoHandshake>
{
	EMTPipeProtoHandshake(uint32_t poolId) : poolId(poolId) { }

	uint32_t poolId;
};

struct EMTPipeProtoTransfer : public EMTPipeProto<kEMTPipeProtoTransfer, EMTPipeProtoTransfer>
{

};

class EMTIPC : public IEMTIPC, public IEMTPipeHandler
{
	IMPL_IEMTUNKNOWN;

public:
	explicit EMTIPC(IEMTThread * thread, IEMTIPCHandler * ipcHandler);

protected: // IEMTIPC
	virtual bool listen(wchar_t *name);
	virtual bool connect(wchar_t *name);

	virtual void * alloc(const uint32_t len);
	virtual void free(void * buf);

	virtual void send(void * buf);

protected: // IEMTPipeHandler
	virtual void connected();
	virtual void disconnected();

	virtual void received(void * buf);
	virtual void sent(void * buf, const uint32_t len);

private:
	void received(EMTPipeProtoHandshake & p);
	void received(EMTPipeProtoTransfer & p);

	void sent(EMTPipeProtoHandshake & p);
	void sent(EMTPipeProtoTransfer & p);

	void sendPack(EMTPipeProtoBase * pack);

private:
	IEMTPipe * mPipe;
	IEMTThread * mThread;
	IEMTIPCHandler * mIPCHandler;
	EMTPOOL mPool;

	bool mConnected;
	uint32_t mRemoteId;
};

EMTIPC::EMTIPC(IEMTThread * thread, IEMTIPCHandler * ipcHandler)
	: mThread(thread)
	, mIPCHandler(ipcHandler)
	, mConnected(false)
{
	constructEMTPool(&mPool, nullptr, 0, 0);
}

bool EMTIPC::listen(wchar_t * name)
{
	wchar_t fullname[MAX_PATH];

	wcscpy_s(fullname, L"\\\\.\\pipe\\");
	wcscat_s(fullname, name);

	return mPipe->listen(fullname);
}

bool EMTIPC::connect(wchar_t * name)
{
	wchar_t fullname[MAX_PATH];

	wcscpy_s(fullname, L"\\\\.\\pipe\\");
	wcscat_s(fullname, name);

	return mPipe->connect(fullname);
}

void * EMTIPC::alloc(const uint32_t len)
{
	return mPool.alloc(&mPool, len);
}

void EMTIPC::free(void * buf)
{
	mPool.free(&mPool, buf);
}

void EMTIPC::send(void * buf)
{
	if (!mThread->isCurrentThread())
		return mThread->queue(createEMTRunnable(std::bind(static_cast<void (EMTIPC::*)(void *)>(&EMTIPC::send), this, buf)), 0);

	const uint32_t len = sizeof(EMTPipeProtoTransfer) + sizeof(uint32_t);
	EMTPipeProtoTransfer * p = new (malloc(len)) EMTPipeProtoTransfer;
	p->len = len;

	uint32_t * t = (uint32_t *)(p + 1);
	*t = mPool.transfer(&mPool, buf, mRemoteId);

	sendPack(p);
}

void EMTIPC::connected()
{
	sendPack(new EMTPipeProtoHandshake(mPool.id(&mPool)));
}

void EMTIPC::disconnected()
{
	mConnected = false;
	mIPCHandler->disconnected();
}

void EMTIPC::received(void * buf)
{
	EMTPipeProtoBase * p = (EMTPipeProtoBase *)buf;
	switch (p->uri)
	{
	case EMTPipeProtoHandshake::kUri:
		received(*(EMTPipeProtoHandshake *)p);
		break;
	case EMTPipeProtoTransfer::kUri:
		received(*(EMTPipeProtoTransfer *)p);
		break;
	default:
		break;
	}
}

void EMTIPC::sent(void * buf, const uint32_t len)
{
	EMTPipeProtoBase * p = (EMTPipeProtoBase *)buf;
	switch (p->uri)
	{
	case EMTPipeProtoHandshake::kUri:
		sent(*(EMTPipeProtoHandshake *)p);
		break;
	case EMTPipeProtoTransfer::kUri:
		sent(*(EMTPipeProtoTransfer *)p);
		break;
	default:
		break;
	}

	delete p;
}

void EMTIPC::received(EMTPipeProtoHandshake & p)
{
	mConnected = true;
	mRemoteId = p.poolId;
	mIPCHandler->connected();
}

void EMTIPC::received(EMTPipeProtoTransfer & p)
{
	const uint32_t count = (p.len - sizeof(p)) / sizeof(uint32_t);
	uint32_t * t = (uint32_t *)(&p + 1);

	mIPCHandler->received(mPool.take(&mPool, *t));
}

void EMTIPC::sent(EMTPipeProtoHandshake & p)
{
}

void EMTIPC::sent(EMTPipeProtoTransfer & p)
{
	const uint32_t count = (p.len - sizeof(p)) / sizeof(uint32_t);
	uint32_t * t = (uint32_t *)(&p + 1);

	mIPCHandler->sent(mPool.take(&mPool, *t));
}

void EMTIPC::sendPack(EMTPipeProtoBase * pack)
{
	mPipe->send(pack, pack->len);
}

END_NAMESPACE_ANONYMOUS

IEMTIPC * createEMTIPC(IEMTIPCHandler * ipcHandler)
{
	return new EMTIPC(nullptr, ipcHandler);
}
