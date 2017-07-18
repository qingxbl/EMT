#include "EMTIPCWin.h"

#include "EMTIPCPrivate.h"

#include <EMTUtil/EMTThread.h>
#include <EMTUtil/EMTShareMemory.h>
#include <EMTUtil/EMTPipe.h>

#include <Windows.h>

#include <memory>

#pragma pack(push, 1)
struct EMTIPCWinPacket
{
	EMTIPCWinPacket(const uint16_t uri, const uint16_t len) : packet_uri(uri), packet_len(len) { }
	uint16_t packet_uri;
	uint16_t packet_len;
};

template <class T>
struct EMTIPCWinPacketT : public EMTIPCWinPacket
{
	EMTIPCWinPacketT() : EMTIPCWinPacket(T::uri, sizeof(T)) { }

	static T * create() { return new (::malloc(sizeof(T))) T; }
};

enum
{
	kEMTIPCWinPacketBegin = 0,
	kEMTIPCWinPacket_Connect,
	kEMTIPCWinPacket_ConnectACK,
	kEMTIPCWinPacketEnd,
};

struct EMTIPCWinPacket_Connect : public EMTIPCWinPacketT<EMTIPCWinPacket_Connect>
{
	enum { uri = kEMTIPCWinPacket_Connect };

	uint32_t connId;
	uint32_t processId;
	uint64_t eventHandle;
};

struct EMTIPCWinPacket_ConnectACK : public EMTIPCWinPacketT<EMTIPCWinPacket_ConnectACK>
{
	enum { uri = kEMTIPCWinPacket_ConnectACK };

	uint64_t eventHandle;
};
#pragma pack(pop)

struct DECLSPEC_NOVTABLE IEMTIPCWinPipe
{
	virtual ~IEMTIPCWinPipe() { }
	virtual bool connect() = 0;
	virtual void disconnect() = 0;
	virtual void send(void * buf, const uint32_t len) = 0;
};

template <bool SERVER>
class EMTIPCWinPipe : public IEMTIPCWinPipe, public IEMTPipeHandler
{
	EMTIMPL_IEMTUNKNOWN;

public:
	explicit EMTIPCWinPipe(EMTIPCWinPrivate * pHost);
	virtual ~EMTIPCWinPipe() { }

protected: // IEMTPipeHandler
	virtual void connected() { }
	virtual void disconnected();

	virtual void received(void * buf, const uint32_t len);
	virtual void sent(void * buf, const uint32_t len);

protected: // IEMTIPCWinPipe
	virtual bool connect();
	virtual void disconnect();
	virtual void send(void * buf, const uint32_t len);

private:
	bool connect(const wchar_t * pName);

private:
	std::unique_ptr<IEMTPipe, IEMTUnknown_Delete> mPipe;

	EMTIPCWinPrivate * mHost;
};

class EMTIPCWinPrivate : public EMTIPCPrivate
{
public:
	virtual ~EMTIPCWinPrivate();

	void init(const wchar_t * pName);

	EMTIPCWin * q() const;
	const wchar_t * name() const;

	void connect();
	void disconnect();

	void received(void * buf, const uint32_t len);

private:
	void sys_notified();

protected:
	friend class EMTIPCWin;
	friend class EMTIPCPrivate;

	wchar_t * mName;

	std::unique_ptr<IEMTWaitable, IEMTUnknown_Delete> mEventLWaitable;
	std::unique_ptr<IEMTIPCWinPipe> mPipe;

	HANDLE mEventL;
	HANDLE mEventR;
};

template <bool SERVER>
EMTIPCWinPipe<SERVER>::EMTIPCWinPipe(EMTIPCWinPrivate * pHost)
	: mPipe(createEMTPipe(pHost->q()->thread(), this))
	, mHost(pHost)
{
}

template<bool SERVER>
void EMTIPCWinPipe<SERVER>::disconnected()
{
	mHost->disconnect();
}

void EMTIPCWinPipe<true>::connected()
{
	mHost->connect();
}

template<bool SERVER>
void EMTIPCWinPipe<SERVER>::received(void * buf, const uint32_t len)
{
	mHost->received(buf, len);
}

template<bool SERVER>
void EMTIPCWinPipe<SERVER>::sent(void * buf, const uint32_t len)
{
	::free(buf);
}

template<bool SERVER>
bool EMTIPCWinPipe<SERVER>::connect()
{
	wchar_t fullname[MAX_PATH] = L"\\\\.\\pipe\\";
	wcscat_s(fullname, mHost->name());

	return connect(fullname);
}

bool EMTIPCWinPipe<true>::connect(const wchar_t * pName)
{
	return mPipe->listen(pName);
}

bool EMTIPCWinPipe<false>::connect(const wchar_t * pName)
{
	return mPipe->connect(pName);
}

template<bool SERVER>
void EMTIPCWinPipe<SERVER>::disconnect()
{
	mPipe->disconnect();
}

template<bool SERVER>
void EMTIPCWinPipe<SERVER>::send(void * buf, const uint32_t len)
{
	mPipe->send(buf, len);
}

EMTIPCWinPrivate::~EMTIPCWinPrivate()
{
	mThread->unregisterWaitable(mEventLWaitable.get());
	::CloseHandle(mEventL);
	if (mEventR != INVALID_HANDLE_VALUE)
		::CloseHandle(mEventR);
}

void EMTIPCWinPrivate::init(const wchar_t * pName)
{
	mName = _wcsdup(pName);
	mEventL = ::CreateEventW(NULL, FALSE, FALSE, NULL);
	mEventR = INVALID_HANDLE_VALUE;

	mEventLWaitable.reset(createEMTWaitable(std::bind(&EMTIPCWinPrivate::sys_notified, this), mEventL));
	mThread->registerWaitable(mEventLWaitable.get());
}

EMTIPCWin * EMTIPCWinPrivate::q() const
{
	return (EMTIPCWin *)q_ptr;
}

const wchar_t * EMTIPCWinPrivate::name() const
{
	return mName;
}

void EMTIPCWinPrivate::connect()
{
	EMTIPCWinPacket_Connect * np = EMTIPCWinPacket_Connect::create();
	np->connId = EMTCore_connect(&mCore, EMTIPC::kInvalidConn);
	np->processId = ::GetCurrentProcessId();
	np->eventHandle = (uintptr_t)mEventL;

	mPipe->send(np, sizeof(*np));
}

void EMTIPCWinPrivate::disconnect()
{
	disconnected();

	EMTCore_disconnect(&mCore);

	if (mEventR != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(mEventR);
		mEventR = INVALID_HANDLE_VALUE;
	}
}

void EMTIPCWinPrivate::received(void * buf, const uint32_t len)
{
	switch (((EMTIPCWinPacket *)buf)->packet_uri)
	{
	case kEMTIPCWinPacket_Connect:
	{
		EMTIPCWinPacket_Connect * p = (EMTIPCWinPacket_Connect *)buf;
		EMTIPCWinPacket_ConnectACK * np = EMTIPCWinPacket_ConnectACK::create();

		HANDLE procL = ::GetCurrentProcess();
		HANDLE procR = ::OpenProcess(PROCESS_DUP_HANDLE, FALSE, p->processId);
		HANDLE eventL2R = INVALID_HANDLE_VALUE;
		::DuplicateHandle(procR, (HANDLE)p->eventHandle, procL, &mEventR, EVENT_MODIFY_STATE, FALSE, 0);
		::DuplicateHandle(procL, mEventL, procR, &eventL2R, EVENT_MODIFY_STATE, FALSE, 0);
		::CloseHandle(procR);

		np->eventHandle = (uintptr_t)eventL2R;
		mPipe->send(np, sizeof(*np));

		EMTCore_connect(&mCore, p->connId);

		connected();
		break;
	}
	case kEMTIPCWinPacket_ConnectACK:
	{
		EMTIPCWinPacket_ConnectACK * p = (EMTIPCWinPacket_ConnectACK *)buf;
		mEventR = (HANDLE)p->eventHandle;

		connected();
		break;
	}
	}
}

void EMTIPCWinPrivate::sys_notified()
{
	notified();
}

void * EMTIPCPrivate::allocSys(EMTIPCPrivate * pThis, const uint32_t uLen)
{
	return ::malloc(uLen);
}

void EMTIPCPrivate::freeSys(EMTIPCPrivate * pThis, void * pMem)
{
	::free(pMem);
}

void EMTIPCPrivate::sys_notify()
{
	EMTIPCWinPrivate * sys = (EMTIPCWinPrivate *)this;

	::SetEvent(sys->mEventR);
}

EMTIPCWin::EMTIPCWin(const wchar_t * pName, IEMTThread * pThread, IEMTIPCSink * pSink)
	: EMTIPC(*new EMTIPCWinPrivate, pThread, createEMTShareMemory(pName), pSink)
{
	EMT_D(EMTIPCWin);

	d->init(pName);
}

EMTIPCWin::~EMTIPCWin()
{

}

bool EMTIPCWin::connect(bool isServer)
{
	EMT_D(EMTIPCWin);

	if (d->mPipe)
		return false;

	std::unique_ptr<IEMTIPCWinPipe> pipeHandler(isServer ? (IEMTIPCWinPipe *)new EMTIPCWinPipe<true>(d) : new EMTIPCWinPipe<false>(d));

	const bool ret = pipeHandler->connect();
	if (ret)
		d->mPipe.swap(pipeHandler);

	return ret;
}

void EMTIPCWin::disconnect()
{
	EMT_D(EMTIPCWin);

	if (!d->mPipe)
		return;

	d->mPipe->disconnect();
}
