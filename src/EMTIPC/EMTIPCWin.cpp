#include "EMTIPCWin.h"

#include "EMTIPCPrivate.h"

#include <EMTUtil/EMTThread.h>
#include <EMTUtil/EMTShareMemory.h>
#include <EMTUtil/EMTPipe.h>

#include <Windows.h>

#include <memory>

struct DECLSPEC_NOVTABLE IEMTIPCWinPipeHandler : public IEMTPipeHandler
{
	virtual bool connect() = 0;
	virtual void disconnect() = 0;
};

template <bool SERVER>
class EMTIPCWinPipeHandler : public IEMTIPCWinPipeHandler
{
	EMTIMPL_IEMTUNKNOWN;

public:
	explicit EMTIPCWinPipeHandler(EMTIPCWinPrivate * pHost);
	virtual ~EMTIPCWinPipeHandler() { }

protected: // IEMTPipeHandler
	virtual void connected() { }
	virtual void disconnected();

	virtual void received(void * buf, const uint32_t len) { }
	virtual void sent(void * buf, const uint32_t len) { }

protected: // IEMTIPCWinPipeHandler
	virtual bool connect();
	virtual void disconnect();

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

private:
	void sys_notified();

protected:
	friend class EMTIPCWin;
	friend class EMTIPCPrivate;

	wchar_t * mName;

	std::unique_ptr<IEMTWaitable, IEMTUnknown_Delete> mEventLWaitable;
	std::unique_ptr<IEMTIPCWinPipeHandler, IEMTUnknown_Delete> mPipeHandler;

	HANDLE mEventL;
	HANDLE mEventR;
};

template <bool SERVER>
EMTIPCWinPipeHandler<SERVER>::EMTIPCWinPipeHandler(EMTIPCWinPrivate * pHost)
	: mPipe(createEMTPipe(pHost->q()->thread(), this))
	, mHost(pHost)
{
}

template<bool SERVER>
void EMTIPCWinPipeHandler<SERVER>::disconnected()
{
	mHost->q()->EMTIPC::disconnect();
}

void EMTIPCWinPipeHandler<true>::connected()
{
	uint32_t * buf = (uint32_t *)::malloc(sizeof(uint32_t));
	*buf = mHost->q()->EMTIPC::connect(EMTIPC::kInvalidConn);
	mPipe->send(buf, sizeof(*buf));
}

void EMTIPCWinPipeHandler<false>::received(void * buf, const uint32_t len)
{
	mHost->q()->EMTIPC::connect(*(uint32_t *)buf);
}

template<bool SERVER>
bool EMTIPCWinPipeHandler<SERVER>::connect()
{
	wchar_t fullname[MAX_PATH] = L"\\\\.\\pipe\\";
	wcscat_s(fullname, mHost->name());

	return connect(fullname);
}

bool EMTIPCWinPipeHandler<true>::connect(const wchar_t * pName)
{
	return mPipe->listen(pName);
}

bool EMTIPCWinPipeHandler<false>::connect(const wchar_t * pName)
{
	return mPipe->connect(pName);
}

template<bool SERVER>
void EMTIPCWinPipeHandler<SERVER>::disconnect()
{
	mPipe->disconnect();
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

uint32_t EMTIPCPrivate::sys_connect(uint32_t uConnId)
{
	EMTIPCWinPrivate * sys = (EMTIPCWinPrivate *)this;
	return EMTCore_connect(&mCore, uConnId, ::GetCurrentProcessId(), (uintptr_t)sys->mEventL);
}

void EMTIPCPrivate::sys_connected(const uint64_t uParam0, const uint64_t uParam1)
{
	EMTIPCWinPrivate * sys = (EMTIPCWinPrivate *)this;

	HANDLE procR = ::OpenProcess(PROCESS_DUP_HANDLE, FALSE, (DWORD)uParam0);
	::DuplicateHandle(procR, (HANDLE)uParam1, ::GetCurrentProcess(), &sys->mEventR, EVENT_MODIFY_STATE, FALSE, 0);
	::CloseHandle(procR);
}

void EMTIPCPrivate::sys_disconnected()
{
	EMTIPCWinPrivate * sys = (EMTIPCWinPrivate *)this;

	if (sys->mEventR != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(sys->mEventR);
		sys->mEventR = INVALID_HANDLE_VALUE;
	}
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

	if (d->mPipeHandler)
		return false;

	std::unique_ptr<IEMTIPCWinPipeHandler, IEMTUnknown_Delete> pipeHandler(isServer ? (IEMTIPCWinPipeHandler *)new EMTIPCWinPipeHandler<true>(d) : new EMTIPCWinPipeHandler<false>(d));

	const bool ret = pipeHandler->connect();
	if (ret)
		d->mPipeHandler.swap(pipeHandler);

	return ret;
}

void EMTIPCWin::disconnect()
{
	EMT_D(EMTIPCWin);

	if (!d->mPipeHandler)
		return;

	d->mPipeHandler->disconnect();
}
