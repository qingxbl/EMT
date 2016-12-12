#include "EMTIPC.h"

#include <EMTUtil/EMTPipe.h>
#include <EMTUtil/EMTThread.h>
#include <EMTUtil/EMTShareMemory.h>
#include <EMTUtil/EMTMultiPool.h>

#include <Windows.h>

#include <memory>
#include <algorithm>

BEGIN_NAMESPACE_ANONYMOUS

enum
{
	kBufferSize = kEMTPipeBufferSize,

	kPageShift = 12,
	kPageSize = 1 << kPageShift,
	kPageMask = kPageSize - 1,

	kBlockLengthMax = 64 * 1024,
	kBlockCountMax = 4,
	kBlockLinearMax = kBlockLengthMax * kBlockCountMax,
};

enum
{
	kEMTPipeProtoBase = 0,
	kEMTPipeProtoHandshake,
	kEMTPipeProtoTransfer,
	kEMTPipeProtoPartialTransfer,
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

};

struct EMTPipeProtoPartialTransfer : public EMTPipeProto<kEMTPipeProtoPartialTransfer, EMTPipeProtoPartialTransfer>
{
	EMTPipeProtoPartialTransfer(const uint32_t total, void * senderBuf)
		: start(0), end(0), total(total), senderHandle((uintptr_t)senderBuf), receiverHandle(0)
	{ }
	uint32_t start;
	uint32_t end;
	uint32_t total;

	uint64_t senderHandle;
	uint64_t receiverHandle;
};

struct EMTIPCTrasferData
{
	uint32_t token;
};

struct EMTIPCMemoryBlock
{
	uint32_t length;
};

struct EMTMULTIPOOL_Delete
{
	void operator()(EMTMULTIPOOL * p) const { emtMultiPool()->destruct(p); }
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
	void received(EMTPipeProtoPartialTransfer & p);

	void sent(EMTPipeProtoHandshake & p);
	void sent(EMTPipeProtoTransfer & p);
	void sent(EMTPipeProtoPartialTransfer & p);

	void sendPack(EMTPipeProtoBase * pack);

	void createPool(const wchar_t * name);
	void deletePool();

	void sendPartial(EMTPipeProtoPartialTransfer * p);
	void receivePartial(const EMTPipeProtoPartialTransfer & p);

private:
	IEMTThread * mThread;
	IEMTIPCHandler * mIPCHandler;
	std::unique_ptr<IEMTPipe, IEMTUnknown_Delete> mPipe;
	std::unique_ptr<IEMTShareMemory, IEMTUnknown_Delete> mShareMemory;
	std::unique_ptr<EMTMULTIPOOL, EMTMULTIPOOL_Delete> mPool;

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
	void * ret = emtMultiPool()->alloc(mPool.get(), len);

	if (ret == nullptr)
	{
		EMTIPCMemoryBlock * memoryBlock = (EMTIPCMemoryBlock *)malloc(sizeof(EMTIPCMemoryBlock) + len);
		memoryBlock->length = len;
		ret = memoryBlock + 1;
	}

	return ret;
}

void EMTIPC::free(void * buf)
{
	EMTPOOL * pool = emtMultiPool()->poolByMem(mPool.get(), buf);
	if (pool)
	{
		emtPool()->free(pool, buf);
	}
	else
	{
		::free((EMTIPCMemoryBlock *)buf - 1);
	}
}

void EMTIPC::send(void * buf)
{
	if (!mThread->isCurrentThread())
		return mThread->queue(createEMTRunnable(std::bind(&EMTIPC::send, this, buf)));

	if (!emtMultiPool()->poolByMem(mPool.get(), buf))
	{
		const uint32_t bufLen = (((EMTIPCMemoryBlock *)buf) - 1)->length;
		return sendPartial(new (malloc(kBufferSize)) EMTPipeProtoPartialTransfer(bufLen, buf));
	}

	const uint32_t len = sizeof(EMTPipeProtoTransfer) + sizeof(EMTIPCTrasferData);
	EMTPipeProtoTransfer * p = new (malloc(len)) EMTPipeProtoTransfer;
	p->len = len;

	EMTIPCTrasferData * t = (EMTIPCTrasferData *)(p + 1);
	t->token = emtMultiPool()->transfer(mPool.get(), buf, mRemoteId);

	sendPack(p);
}

void EMTIPC::connected()
{
	sendPack(new EMTPipeProtoHandshake(emtMultiPool()->id(mPool.get())));
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
	case EMTPipeProtoPartialTransfer::kUri:
		received(*(EMTPipeProtoPartialTransfer *)p);
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
	case EMTPipeProtoPartialTransfer::kUri:
		sent(*(EMTPipeProtoPartialTransfer *)p);
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
	const uint32_t count = (p.len - sizeof(p)) / sizeof(EMTIPCTrasferData);
	EMTIPCTrasferData * t = (EMTIPCTrasferData *)(&p + 1);

	mIPCHandler->received(emtMultiPool()->take(mPool.get(), t->token));
}

void EMTIPC::received(EMTPipeProtoPartialTransfer & p)
{
	if (p.start != p.end)
		receivePartial(p);
	else
		sendPartial(new (malloc(kBufferSize)) EMTPipeProtoPartialTransfer(p));
}

void EMTIPC::sent(EMTPipeProtoHandshake & p)
{
}

void EMTIPC::sent(EMTPipeProtoTransfer & p)
{
	const uint32_t count = (p.len - sizeof(p)) / sizeof(EMTIPCTrasferData);
	EMTIPCTrasferData * t = (EMTIPCTrasferData *)(&p + 1);

	mIPCHandler->sent(emtMultiPool()->take(mPool.get(), t->token));
}

void EMTIPC::sent(EMTPipeProtoPartialTransfer & p)
{
	if (p.end == p.total)
		mIPCHandler->sent((void *)p.senderHandle);
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

	EMTMULTIPOOLCONFIG multiPoolConfig[] =
	{
		{ 512, 8 * 128, 4 },
		{ kBlockLengthMax, 16 * 8, kBlockCountMax },
	};
	EMTMULTIPOOL * multiPool = (EMTMULTIPOOL *)malloc(sizeof(EMTMULTIPOOL) + sizeof(multiPoolConfig));
	multiPool->uPoolCount = _ARRAYSIZE(multiPoolConfig);
	memcpy(multiPool + 1, multiPoolConfig, sizeof(multiPoolConfig));
	uint32_t metaLen, memLen;
	emtMultiPool()->calcMetaSize(multiPool, &metaLen, &memLen);
	metaLen = (metaLen + kPageMask) & kPageMask;

	void * shareMemory = mShareMemory->open(fullname, metaLen + memLen);

	emtMultiPool()->construct(multiPool, shareMemory, (uint8_t *)shareMemory + metaLen);

	mPool.reset(multiPool);
}

void EMTIPC::deletePool()
{
	if (!mShareMemory)
		return;

	mPool.reset();
	mShareMemory.reset();
}

void EMTIPC::sendPartial(EMTPipeProtoPartialTransfer * p)
{
	EMTIPCTrasferData * t = (EMTIPCTrasferData *)(p + 1);
	EMTIPCTrasferData * tend = (EMTIPCTrasferData *)((uint8_t *)p + kBufferSize);

	for (; t < tend && p->end < p->total; ++t)
	{
		const uint32_t blockLen = min(p->total - p->end, kBlockLinearMax);
		void * buf = emtMultiPool()->alloc(mPool.get(), blockLen);
		if (!buf)
			break;

		memcpy(buf, (uint8_t *)p->senderHandle + p->end, blockLen);
		p->end += blockLen;
		p->len += sizeof(*t);
		t->token = emtMultiPool()->transfer(mPool.get(), buf, mRemoteId);
	}

	if (p->len != sizeof(*p))
	{
		sendPack(p);
	}
	else
	{
		mThread->queue(createEMTRunnable(std::bind(&EMTIPC::sendPartial, this, p)));
	}
}

void EMTIPC::receivePartial(const EMTPipeProtoPartialTransfer & p)
{
	void * mem;

	if (p.start != 0 || p.len != sizeof(p) + sizeof(EMTIPCTrasferData) || p.end != p.total)
	{
		uint32_t start = p.start;
		mem = (p.receiverHandle ? (void *)p.receiverHandle : (uint8_t *)this->alloc(p.total));

		std::for_each((EMTIPCTrasferData *)(&p + 1), (EMTIPCTrasferData *)(&p + 1) + ((p.len - sizeof(p)) / sizeof(EMTIPCTrasferData)), [&](const EMTIPCTrasferData & t)
		{
			void * buf = emtMultiPool()->take(mPool.get(), t.token);
			const uint32_t len = emtMultiPool()->length(mPool.get(), buf);

			memcpy((uint8_t *)mem + start, buf, len);
			start += len;

			emtMultiPool()->free(mPool.get(), buf);
		});
	}
	else
	{
		EMTIPCTrasferData * t = (EMTIPCTrasferData *)(&p + 1);
		mem = emtMultiPool()->take(mPool.get(), t->token);
	}

	if (p.end == p.total)
	{
		mIPCHandler->received(mem);
	}
	else
	{
		EMTPipeProtoPartialTransfer * np = new EMTPipeProtoPartialTransfer(p);
		np->len = sizeof(EMTPipeProtoPartialTransfer);
		np->start = p.end;
		np->receiverHandle = (uintptr_t)mem;
		sendPack(np);
	}
}

END_NAMESPACE_ANONYMOUS

IEMTIPC * createEMTIPC(IEMTThread * thread, IEMTIPCHandler * ipcHandler)
{
	return new EMTIPC(thread, ipcHandler);
}
