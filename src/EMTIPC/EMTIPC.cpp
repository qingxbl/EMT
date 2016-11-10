#include "EMTIPC.h"

#include <EMTUtil/EMTPipe.h>
#include <EMTUtil/EMTThread.h>
#include <EMTUtil/EMTShareMemory.h>
#include <EMTUtil/EMTPool.h>
#include <EMTUtil/EMTPoolSupport.h>

#include <Windows.h>

#include <memory>

BEGIN_NAMESPACE_ANONYMOUS

enum
{
	kDefaultTimeout = 5000,
	kBufferSize = 512,
	kMemoryPoolSize = 8 * 1024 * 1024,
	kMemoryPoolSizeSmall = 2 * 1024 * 1024,
	kMemoryPoolSizeBig = kMemoryPoolSize - kMemoryPoolSizeSmall,
	kMemoryBlockSizeLimitFactor = 4,
	kMemoryBlockSizeSmall = 4 * 1024,
	kMemoryBlockSizeSmallLimit = kMemoryBlockSizeSmall * kMemoryBlockSizeLimitFactor,
	kMemoryBlockSizeBig = 64 * 1024,
	kMemoryBlockSizeBigLimit = kMemoryBlockSizeBig * kMemoryBlockSizeLimitFactor,
};

enum
{
	kEMTPipeProtoBase = 0,
	kEMTPipeProtoHandshake,
	kEMTPipeProtoTransfer,
};

struct EMTPipeProtoBase
{
	EMTPipeProtoBase(const uint16_t uri, const uint16_t len) : uri(uri), len(len) { }

	uint16_t uri;
	uint16_t len;
};

template <uint16_t URI, class T>
struct EMTPipeProto : EMTPipeProtoBase
{
	typedef T FinalT;
	enum { kUri = URI };

	EMTPipeProto() : EMTPipeProtoBase(kUri, sizeof(FinalT)) { }
};

struct EMTPipeProtoHandshake : public EMTPipeProto<kEMTPipeProtoHandshake, EMTPipeProtoHandshake>
{
	EMTPipeProtoHandshake(const uint32_t poolId) : poolId(poolId) { }

	uint32_t poolId;
};

struct EMTPipeProtoTransfer : public EMTPipeProto<kEMTPipeProtoTransfer, EMTPipeProtoTransfer>
{
	EMTPipeProtoTransfer() { }
};

struct EMTIPCBlock
{
	uint32_t poolId;
	uint32_t token;
};

struct EMTIPCTransfer
{
	void * buf;
};

class EMTIPC : public IEMTIPC, public IEMTPipeHandler
{
	IMPL_IEMTUNKNOWN;

public:
	explicit EMTIPC(IEMTThread * thread, IEMTIPCHandler * ipcHandler);
	virtual ~EMTIPC();

protected: // IEMTIPC
	virtual bool isConnected();

	virtual bool listen(const wchar_t *name);
	virtual bool connect(const wchar_t *name);
	virtual void disconnect();

	virtual void * alloc(const uint32_t len);
	virtual void free(void * buf);

	virtual void send(void * buf);

protected: // IEMTPipeHandler
	virtual void connected();
	virtual void disconnected();

	virtual void received(void * buf, const uint32_t len);
	virtual void sent(void * buf, const uint32_t len);

private:
	bool open(const wchar_t * name, const bool isServer);

private:
	void received(EMTPipeProtoHandshake & p);
	void received(EMTPipeProtoTransfer & p);

	void sent(EMTPipeProtoHandshake & p);
	void sent(EMTPipeProtoTransfer & p);

	void sendPack(EMTPipeProtoBase * pack);

	void createPool(const wchar_t * name);
	void deletePool();

private:
	IEMTThread * mThread;
	IEMTIPCHandler * mIPCHandler;
	std::unique_ptr<IEMTPipe, IEMTUnknown_Delete> mPipe;
	std::unique_ptr<IEMTShareMemory, IEMTUnknown_Delete> mShareMemory;
	std::unique_ptr<IEMTMultiPool, IEMTUnknown_Delete> mPool;

	bool mConnected;
	uint32_t mRemoteId;
};

EMTIPC::EMTIPC(IEMTThread * thread, IEMTIPCHandler * ipcHandler)
	: mThread(thread)
	, mIPCHandler(ipcHandler)
	, mPipe(createEMTPipe(thread, this))
	, mConnected(false)
{

}

EMTIPC::~EMTIPC()
{
	deletePool();
}

bool EMTIPC::isConnected()
{
	return mConnected;
}

bool EMTIPC::listen(const wchar_t * name)
{
	return open(name, true);
}

bool EMTIPC::connect(const wchar_t * name)
{
	return open(name, false);
}

void EMTIPC::disconnect()
{
	if (!mThread->isCurrentThread())
		return mThread->queue(createEMTRunnable(std::bind(&EMTIPC::disconnect, this)));

	if (!mConnected)
		deletePool();

	mPipe->disconnect();
}

void * EMTIPC::alloc(const uint32_t len)
{
	return mPool->alloc(len);
}

void EMTIPC::free(void * buf)
{
	mPool->free(buf);
}

void EMTIPC::send(void * buf)
{
	if (!mThread->isCurrentThread())
		return mThread->queue(createEMTRunnable(std::bind(&EMTIPC::send, this, buf)));

	const uint32_t protoLen = sizeof(EMTPipeProtoTransfer) + sizeof(EMTIPCBlock);
	const uint32_t len = protoLen + sizeof(EMTIPCTransfer);
	EMTPipeProtoTransfer * p = new (malloc(len)) EMTPipeProtoTransfer;
	p->len = protoLen;

	EMTIPCTransfer * f = (EMTIPCTransfer *)((uint8_t *)p) + protoLen;
	f->buf = buf;

	EMTIPCBlock * t = (EMTIPCBlock *)(p + 1);

	const bool partial = mPool->transfer(buf, mRemoteId, &t->token, &t->poolId);
	sendPack(p);
	if (partial)
		mPool->transferPartial(buf, mRemoteId, &t->token, &t->poolId);
}

void EMTIPC::connected()
{
	sendPack(new EMTPipeProtoHandshake(mPool->id()));
}

void EMTIPC::disconnected()
{
	deletePool();
	mConnected = false;

	mIPCHandler->disconnected();
}

void EMTIPC::received(void * buf, const uint32_t len)
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

bool EMTIPC::open(const wchar_t * name, const bool isServer)
{
	wchar_t fullname[MAX_PATH];

	wcscpy_s(fullname, L"\\\\.\\pipe\\");
	wcscat_s(fullname, name);

	const bool pipeResult = isServer ? mPipe->listen(fullname) : mPipe->connect(fullname);
	if (!pipeResult)
		return false;

	wcscpy_s(fullname, L"Local\\");
	wcscat_s(fullname, name);

	createPool(fullname);

	return true;
}

void EMTIPC::received(EMTPipeProtoHandshake & p)
{
	mConnected = true;
	mRemoteId = p.poolId;
	mIPCHandler->connected();
}

void EMTIPC::received(EMTPipeProtoTransfer & p)
{
	const uint32_t count = (p.len - sizeof(p)) / sizeof(EMTIPCBlock);
	EMTIPCBlock * t = (EMTIPCBlock *)(&p + 1);

	mIPCHandler->received(mPool->take(t->token, t->poolId));
}

void EMTIPC::sent(EMTPipeProtoHandshake & p)
{
}

void EMTIPC::sent(EMTPipeProtoTransfer & p)
{
	const uint32_t count = (p.len - sizeof(p)) / sizeof(EMTIPCBlock);
	EMTIPCTransfer * f = (EMTIPCTransfer *)((uint8_t *)&p + p.len);

	mIPCHandler->sent(f->buf);
}

void EMTIPC::sendPack(EMTPipeProtoBase * pack)
{
	mPipe->send(pack, pack->len);
}

void EMTIPC::createPool(const wchar_t * fullname)
{
	if (mShareMemory)
		return;

	mShareMemory.reset(createEMTShareMemory());

	void * shareMemory = mShareMemory->open(fullname, kMemoryPoolSize);

	mPool.reset(createEMTMultiPool());
	mPool->init(shareMemory);
}

void EMTIPC::deletePool()
{
	if (!mShareMemory)
		return;

	mPool.release();
	mShareMemory.release();
}

END_NAMESPACE_ANONYMOUS

IEMTIPC * createEMTIPC(IEMTThread * thread, IEMTIPCHandler * ipcHandler)
{
	return new EMTIPC(thread, ipcHandler);
}
