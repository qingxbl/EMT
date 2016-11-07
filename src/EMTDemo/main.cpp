#include <EMTUtil/EMTThread.h>
#include <EMTIPC/EMTIPC.h>

#include <process.h>
#include <windows.h>

#include <memory>

enum
{
	kTestCount = 1000000,
	kTestBufferSize = 128 * 1096,
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
	IPCHandler(const wchar_t * name);
	~IPCHandler();

	bool isReady() const { return mInited != 0; }
	const wchar_t * name() const { return mName; }

	IEMTThread * thread() const { return mThread.lock().get(); }
	IEMTIPC * IPC() const { return mIPC.lock().get(); }

	HANDLE handleThread() const { return mHandleThread; }

protected: // IEMTIPCHandler
	virtual void connected();
	virtual void disconnected();

	virtual void received(void * buf);
	virtual void sent(void * buf);

private:
	static void thread_entry(void * arg);

private:
	volatile uint32_t mInited;
	const wchar_t * mName;
	std::weak_ptr<IEMTThread> mThread;
	std::weak_ptr<IEMTIPC> mIPC;

	HANDLE mHandleThread;

	int mReceivedCount;
	int mSendCount;
	FILETIME mTimeReceiveStart;
	FILETIME mTimeReceiveEnd;
};

IPCHandler::IPCHandler(const wchar_t * name)
	: mInited(0)
	, mName(name)
	, mReceivedCount(0)
	, mSendCount(0)
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

	++mSendCount;

	if (mSendCount == kTestCount && mReceivedCount == kTestCount)
		IPC()->disconnect();
}

void IPCHandler::thread_entry(void * arg)
{
	IPCHandler * pThis = (IPCHandler *)arg;

	std::shared_ptr<IEMTThread> thread(createEMTThread(), IEMTUnknown_Delete());
	std::shared_ptr<IEMTIPC> ipc(createEMTIPC(thread.get(), pThis), IEMTUnknown_Delete());

	pThis->mThread = thread;
	pThis->mIPC = ipc;

	if (!ipc->connect(pThis->mName))
		ipc->listen(pThis->mName);

	::InterlockedIncrement(&pThis->mInited);

	thread->exec();
}

int main(int /*argc*/, char* /*argv*/[])
{
	wchar_t ipcName[] = L"EMTDemo";

	IPCHandler ipcHandler(ipcName);
	while (!ipcHandler.isReady());

	IEMTIPC * ipc = ipcHandler.IPC();
	while (!ipc->isConnected());

	FILETIME timeSendStart;
	FILETIME timeSendEnd;
	char input[kTestBufferSize];
	input[0] = 'a';
	input[1] = '\0';

	//while (scanf_s("%s", input, (unsigned)_countof(input)) != EOF)
	::GetSystemTimePreciseAsFileTime(&timeSendStart);
	for (int i = 0; i < kTestCount; ++i)
	{
		char * buf = (char *)ipc->alloc(sizeof(input));

		memcpy(buf, input, sizeof(input));

		ipc->send(buf);
	}
	::GetSystemTimePreciseAsFileTime(&timeSendEnd);
	timeUsage("Send %llu\n", timeSendStart, timeSendEnd);

	::WaitForSingleObject(ipcHandler.handleThread(), INFINITE);

	return 0;
}
