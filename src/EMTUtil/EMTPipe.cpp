#include "stable.h"

#include "EMTPipe.h"

#include "EMTThread.h"

#include <Windows.h>

BEGIN_NAMESPACE_ANONYMOUS

static void createEvent(PHANDLE h)
{
	if (*h != INVALID_HANDLE_VALUE)
		return;

	*h = ::CreateEventW(NULL, TRUE, FALSE, NULL);
}

static void closeHandle(PHANDLE h)
{
	if (*h == INVALID_HANDLE_VALUE)
		return;

	::CloseHandle(*h);
	*h = INVALID_HANDLE_VALUE;
}

template <class T>
struct OVERLAPPEDEX : public OVERLAPPED
{
	T * context;
	uint32_t len;
	void * buffer;
};

template <class T>
static OVERLAPPEDEX<T> * createOverlappedEx(T * context, const uint32_t len, void * buffer = nullptr)
{
	OVERLAPPEDEX<T> * overlap = new (malloc(sizeof(OVERLAPPEDEX<T>) + (buffer ? 0 : len))) OVERLAPPEDEX<T>;
	memset(overlap, 0, sizeof(OVERLAPPED));
	overlap->context = context;
	overlap->len = len;
	overlap->buffer = buffer ? buffer : overlap + 1;

	return overlap;
}

template <class T>
static void destroyOverlappedEx(OVERLAPPEDEX<T> * pThis)
{
	pThis->~OVERLAPPEDEX<T>();
	free(pThis);
}

enum
{
	kEMTPipeDisconnected,
	kEMTPipeDisconnecting,
	kEMTPipeConnected,
	kEMTPipeConnecting,
};

class EMTPipe : public IEMTPipe
{
	EMTIMPL_IEMTUNKNOWN;

public:
	typedef OVERLAPPEDEX<EMTPipe> OVERLAPPED_EMTPipe;

public:
	explicit EMTPipe(IEMTThread * thread, IEMTPipeHandler * pipeHandler, const uint32_t bufferSize, const uint32_t timeout);
	virtual ~EMTPipe();

	IEMTThread * thread() const { return mThread; }

	void connected();

protected: // IEMTPipe
	virtual bool isConnected();

	virtual bool listen(const wchar_t * name);
	virtual bool connect(const wchar_t * name);
	virtual void disconnect();

	virtual void send(void * buf, const uint32_t len);

private:
	uint32_t receive();
	void disconnectWithNotify();

	void received(DWORD errorCode, DWORD numberOfBytesTransfered, OVERLAPPED_EMTPipe * overlapped);
	void sent(DWORD errorCode, DWORD numberOfBytesTransfered, OVERLAPPED_EMTPipe * overlapped);

	bool handleError(DWORD errorCode);

private:
	static void WINAPI readComplete(DWORD errorCode, DWORD numberOfBytesTransfered, LPOVERLAPPED overlapped);
	static void WINAPI writeComplete(DWORD errorCode, DWORD numberOfBytesTransfered, LPOVERLAPPED overlapped);

private:
	IEMTThread * mThread;
	IEMTPipeHandler * mPipeHandler;
	const uint32_t mBufferSize;
	const uint32_t mTimeout;
	HANDLE mPipe;
	uint32_t mStatus;

	std::unique_ptr<IEMTWaitable, IEMTUnknown_Delete> mWaitConnect;
};

class EMTPipeWaitConnect : public IEMTWaitable
{
	EMTIMPL_IEMTUNKNOWN;

public:
	explicit EMTPipeWaitConnect(EMTPipe * pipe);
	virtual ~EMTPipeWaitConnect();

	LPOVERLAPPED overlapped();

protected: // IEMTRunnable
	virtual void run();
	virtual bool isAutoDestroy();

protected: // IEMTWaitable
	virtual void * waitHandle();

private:
	EMTPipe * mPipe;
	OVERLAPPED mOverlapped;
};

EMTPipe::EMTPipe(IEMTThread * thread, IEMTPipeHandler * pipeHandler, const uint32_t bufferSize, const uint32_t timeout)
	: mThread(thread)
	, mPipeHandler(pipeHandler)
	, mBufferSize(bufferSize)
	, mTimeout(timeout)
	, mPipe(INVALID_HANDLE_VALUE)
	, mStatus(kEMTPipeDisconnected)
{

}

EMTPipe::~EMTPipe()
{
	disconnectWithNotify();
}

void EMTPipe::connected()
{
	mWaitConnect.release();

	mStatus = kEMTPipeConnected;
	mPipeHandler->connected();

	EMTPipe::handleError(receive());
}

bool EMTPipe::isConnected()
{
	return mStatus == kEMTPipeConnected && ::GetNamedPipeHandleState(mPipe, NULL, NULL, NULL, NULL, NULL, NULL) != FALSE;
}

bool EMTPipe::listen(const wchar_t * name)
{
	if (mStatus != kEMTPipeDisconnected)
		return false;

	mPipe = ::CreateNamedPipeW(
		name,                     // pipe name
		PIPE_ACCESS_DUPLEX |      // read/write access
		FILE_FLAG_OVERLAPPED,     // overlapped mode
		PIPE_TYPE_MESSAGE |       // message-type pipe
		PIPE_READMODE_MESSAGE |   // message read mode
		PIPE_WAIT,                // blocking mode
		PIPE_UNLIMITED_INSTANCES, // unlimited instances
		mBufferSize,              // output buffer size
		mBufferSize,              // input buffer size
		mTimeout,                 // client time-out
		NULL);                    // default security attributes

	if (mPipe == INVALID_HANDLE_VALUE)
		return false;

	std::unique_ptr<EMTPipeWaitConnect, IEMTUnknown_Delete> waitConnect(new EMTPipeWaitConnect(this));

	if (::ConnectNamedPipe(mPipe, waitConnect->overlapped()))
		return false;

	bool ret = true;
	const DWORD err = ::GetLastError();
	if (err == ERROR_PIPE_CONNECTED)
	{
		mThread->queue(waitConnect.get());
	}
	else if (err == ERROR_IO_PENDING)
	{
		mThread->registerWaitable(waitConnect.get());
	}
	else
	{
		ret = false;
	}

	mWaitConnect.reset(waitConnect.release());
	mStatus = ret ? kEMTPipeConnecting : kEMTPipeDisconnected;
	return ret;
}

bool EMTPipe::connect(const wchar_t * name)
{
	if (mStatus != kEMTPipeDisconnected)
		return false;

	do
	{
		mPipe = ::CreateFileW(
			name,           // pipe name 
			GENERIC_READ |  // read and write access 
			GENERIC_WRITE,
			0,              // no sharing 
			NULL,           // default security attributes
			OPEN_EXISTING,  // opens existing pipe 
			FILE_FLAG_OVERLAPPED,              // default attributes 
			NULL);          // no template file 

		if (mPipe == INVALID_HANDLE_VALUE)
			break;

		DWORD mode = PIPE_READMODE_MESSAGE | PIPE_WAIT;
		BOOL success = SetNamedPipeHandleState(
			mPipe,    // pipe handle 
			&mode,    // new pipe mode 
			NULL,     // don't set maximum bytes 
			NULL);    // don't set maximum time 
		if (!success)
			break;

		mStatus = kEMTPipeConnecting;
		mWaitConnect.reset(new EMTPipeWaitConnect(this));
		mThread->queue(mWaitConnect.get());

		return true;
	} while (true);

	closeHandle(&mPipe);
	return false;
}

void EMTPipe::disconnect()
{
	mWaitConnect.reset();
	closeHandle(&mPipe);

	if (mStatus != kEMTPipeDisconnected)
		mStatus = kEMTPipeDisconnecting;
}

void EMTPipe::send(void * buf, const uint32_t len)
{
	OVERLAPPED_EMTPipe * overlap = createOverlappedEx(this, len, buf);

	::WriteFileEx(mPipe, overlap->buffer, overlap->len, overlap, &writeComplete);
}

uint32_t EMTPipe::receive()
{
	OVERLAPPED_EMTPipe * overlap = createOverlappedEx(this, mBufferSize);

	if (::ReadFileEx(mPipe, overlap->buffer, overlap->len, overlap, &readComplete) == FALSE)
		return ::GetLastError();

	return ERROR_SUCCESS;
}

void EMTPipe::disconnectWithNotify()
{
	disconnect();

	if (mStatus == kEMTPipeDisconnecting)
	{
		mPipeHandler->disconnected();
		mStatus = kEMTPipeDisconnected;
	}
}

void EMTPipe::received(DWORD errorCode, DWORD numberOfBytesTransfered, OVERLAPPED_EMTPipe * overlapped)
{
	if (!handleError(errorCode))
		return;

	const uint32_t err = receive();

	mPipeHandler->received(overlapped->buffer, numberOfBytesTransfered);

	handleError(err);
}

void EMTPipe::sent(DWORD errorCode, DWORD numberOfBytesTransfered, OVERLAPPED_EMTPipe * overlapped)
{
	if (!handleError(errorCode))
		return;

	mPipeHandler->sent(overlapped->buffer, overlapped->len);
}

bool EMTPipe::handleError(DWORD errorCode)
{
	switch (errorCode)
	{
	case ERROR_SUCCESS:
		return true;
	case ERROR_BROKEN_PIPE:
	case ERROR_INVALID_HANDLE:
		disconnectWithNotify();
		return false;
	default:
		::MessageBox(NULL, L"", NULL, 0);
	}

	return false;
}

void EMTPipe::readComplete(DWORD errorCode, DWORD numberOfBytesTransfered, LPOVERLAPPED overlapped)
{
	OVERLAPPED_EMTPipe * o = static_cast<OVERLAPPED_EMTPipe *>(overlapped);
	o->context->received(errorCode, numberOfBytesTransfered, o);
	destroyOverlappedEx(o);
}

void EMTPipe::writeComplete(DWORD errorCode, DWORD numberOfBytesTransfered, LPOVERLAPPED overlapped)
{
	OVERLAPPED_EMTPipe * o = static_cast<OVERLAPPED_EMTPipe *>(overlapped);
	o->context->sent(errorCode, numberOfBytesTransfered, o);
	destroyOverlappedEx(o);
}

EMTPipeWaitConnect::EMTPipeWaitConnect(EMTPipe * pipe)
	: mPipe(pipe)
{
	memset(&mOverlapped, 0, sizeof(mOverlapped));
	mOverlapped.hEvent = INVALID_HANDLE_VALUE;
}

EMTPipeWaitConnect::~EMTPipeWaitConnect()
{
	if (mOverlapped.hEvent != INVALID_HANDLE_VALUE)
		mPipe->thread()->unregisterWaitable(this);

	closeHandle(&mOverlapped.hEvent);
}

LPOVERLAPPED EMTPipeWaitConnect::overlapped()
{
	createEvent(&mOverlapped.hEvent);
	return &mOverlapped;
}

void EMTPipeWaitConnect::run()
{
	mPipe->connected();
}

bool EMTPipeWaitConnect::isAutoDestroy()
{
	return true;
}

void * EMTPipeWaitConnect::waitHandle()
{
	return mOverlapped.hEvent;
}

END_NAMESPACE_ANONYMOUS

IEMTPipe * createEMTPipe(IEMTThread * thread, IEMTPipeHandler * pipeHandler, const uint32_t bufferSize /*= kEMTPipeBufferSize*/, const uint32_t timeout /*= kEMTPipeDefaultTimeout*/)
{
	return new EMTPipe(thread, pipeHandler, bufferSize, timeout);
}
