#include <EMTUtil/EMTThread.h>
#include <EMTIPC/EMTIPC.h>

#include <process.h>
#include <windows.h>

#include <memory>

enum
{
	kTestCount = 1000,
	kTestBufferSize = 512 * 1024,
	kTestSendConcurrent = 100,
};

static void timeUsage(const char *fmt, const FILETIME &start, const FILETIME &end)
{
	LONGLONG diffInTicks =
		reinterpret_cast<const LARGE_INTEGER *>(&end)->QuadPart -
		reinterpret_cast<const LARGE_INTEGER *>(&start)->QuadPart;
	LONGLONG diffInMillis = diffInTicks / 10000;

	printf(fmt, diffInMillis);
}

class IPCHandler : public IEMTIPCHandler
{
	IMPL_IEMTUNKNOWN;

public:
	enum HandlerType : uint32_t
	{
		kTypeUnknown = 0,
		kTypeServer,
		kTypeClient,
	};

public:
	IPCHandler(const wchar_t * name);
	~IPCHandler();

	HandlerType handlerType() const { return mType; }
	const wchar_t * name() const { return mName; }

	IEMTThread * thread() const { return mThread.lock().get(); }
	IEMTIPC * IPC() const { return mIPC.lock().get(); }

	HANDLE handleThread() const { return mHandleThread; }

	void send(const uint32_t count = 1);

protected: // IEMTIPCHandler
	virtual void connected();
	virtual void disconnected();

	virtual void received(void * buf);
	virtual void sent(void * buf);

private:
	static void thread_entry(void * arg);

private:
	volatile HandlerType mType;
	const wchar_t * mName;
	std::weak_ptr<IEMTThread> mThread;
	std::weak_ptr<IEMTIPC> mIPC;

	HANDLE mHandleThread;

	int mReceivedCount;
	int mSendCount;
	int mSentCount;
	FILETIME mTimeReceiveStart;
	FILETIME mTimeReceiveEnd;
	FILETIME mTimeSendStart;
	FILETIME mTimeSendEnd;

	uint8_t mInput[kTestBufferSize];
};

IPCHandler::IPCHandler(const wchar_t * name)
	: mType(kTypeUnknown)
	, mName(name)
	, mReceivedCount(0)
	, mSendCount(0)
	, mSentCount(0)
{
	mHandleThread = (HANDLE)_beginthread(thread_entry, 0, this);
}

IPCHandler::~IPCHandler()
{
	if (!mThread.expired())
	{
		IPC()->disconnect();
		thread()->exit();
	}

	while (!mThread.expired());
}

void IPCHandler::send(const uint32_t count/* = 1*/)
{
	if (mSendCount == 0)
		::GetSystemTimePreciseAsFileTime(&mTimeSendStart);

	IEMTIPC * ipc = IPC();

	for (uint32_t i = 0; i < count; ++i)
	{
		if (mSendCount >= kTestCount)
			return;

		++mSendCount;

		char * buf = (char *)ipc->alloc(kTestBufferSize);

		memcpy(buf, mInput, sizeof(mInput));

		ipc->send(buf);
	}
}

void IPCHandler::connected()
{
	printf("[EMTIPC] Connected.\n");
}

void IPCHandler::disconnected()
{
	printf("[EMTIPC] Disconnected.\n");

	thread()->exit();
}

void IPCHandler::received(void * buf)
{
	//printf("[EMTIPC] %p received.\n", buf);
	//printf("%s\n", (const char *)buf);

	IPC()->free(buf);
	switch (++mReceivedCount)
	{
	case 1:
		::GetSystemTimePreciseAsFileTime(&mTimeReceiveStart);
		break;
	case kTestCount:
		::GetSystemTimePreciseAsFileTime(&mTimeReceiveEnd);
		timeUsage("Received %llu\n", mTimeReceiveStart, mTimeReceiveEnd);
		if (mSendCount == kTestCount)
			IPC()->disconnect();
		break;
	}
}

void IPCHandler::sent(void * buf)
{
	//printf("[EMTIPC] %p sent.\n", buf);

	switch (++mSentCount)
	{
	case kTestCount:
		::GetSystemTimePreciseAsFileTime(&mTimeSendEnd);
		timeUsage("Send %llu\n", mTimeSendStart, mTimeSendEnd);
		IPC()->disconnect();
		break;
	default:
		send();
	}
}

void IPCHandler::thread_entry(void * arg)
{
	IPCHandler * pThis = (IPCHandler *)arg;

	std::shared_ptr<IEMTThread> thread(createEMTThread(), IEMTUnknown_Delete());
	std::shared_ptr<IEMTIPC> ipc(createEMTIPC(thread.get(), pThis), IEMTUnknown_Delete());

	pThis->mThread = thread;
	pThis->mIPC = ipc;

	HandlerType type = kTypeClient;
	if (!ipc->connect(pThis->mName))
	{
		type = kTypeServer;
		ipc->listen(pThis->mName);
	}

	::InterlockedExchange((uint32_t *)&pThis->mType, type);

	thread->exec();
}

int main(int /*argc*/, char* /*argv*/[])
{
	wchar_t ipcName[] = L"EMTDemo";

	IPCHandler ipcHandler(ipcName);
	while (ipcHandler.handlerType() == IPCHandler::kTypeUnknown);

	IEMTIPC * ipc = ipcHandler.IPC();
	while (!ipc->isConnected());

	if (ipcHandler.handlerType() == IPCHandler::kTypeServer)
		ipcHandler.send(kTestSendConcurrent);

	::WaitForSingleObject(ipcHandler.handleThread(), INFINITE);

	return 0;
}
