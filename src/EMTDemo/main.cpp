#include <EMTUtil/EMTThread.h>
#include <EMTIPC/EMTIPCWin.h>
#include <EMTUtil/EMTPool.h>

#include <process.h>
#include <windows.h>

#include <memory>

enum
{
	kTestCount = 1000000,
	kTestBufferSize = 512,
	//kTestSendConcurrent = 2 * 1024 * 64,
	kTestSendConcurrent = 1000,
};

static void timeUsage(const char *fmt, const FILETIME &start, const FILETIME &end)
{
	LONGLONG diffInTicks =
		reinterpret_cast<const LARGE_INTEGER *>(&end)->QuadPart -
		reinterpret_cast<const LARGE_INTEGER *>(&start)->QuadPart;
	LONGLONG diffInMillis = diffInTicks / 10000;

	printf(fmt, diffInMillis);
}

class IPCHandler : public IEMTIPCSink
{
	EMTIMPL_IEMTUNKNOWN;

public:
	enum HandlerType : uint32_t
	{
		kTypeUnknown = 0,
		kTypeServer,
		kTypeClient,
	};

public:
	IPCHandler(const wchar_t * name, const std::weak_ptr<IEMTThread> & thread);
	~IPCHandler();

	HandlerType handlerType() const { return mType; }
	const wchar_t * name() const { return mName; }

	IEMTThread * thread() const { return mThread.lock().get(); }
	EMTIPCWin * IPC() const { return mIPC.get(); }

	HANDLE handleThread() const { return mHandleThread; }

	HandlerType connect();
	void send(const uint32_t count = 1);

protected: // IEMTIPCSink
	virtual void connected();
	virtual void disconnected();

	virtual void received(void * buf, const uint64_t uParam0, const uint64_t uParam1);

private:
	static void thread_entry(void * arg);

private:
	volatile HandlerType mType;
	const wchar_t * mName;
	std::weak_ptr<IEMTThread> mThread;
	std::unique_ptr<EMTIPCWin> mIPC;

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

IPCHandler::IPCHandler(const wchar_t * name, const std::weak_ptr<IEMTThread> & thread)
	: mType(kTypeUnknown)
	, mName(name)
	, mThread(thread)
	, mIPC(new EMTIPCWin(name, thread.lock().get(), this))
	, mReceivedCount(0)
	, mSendCount(0)
	, mSentCount(0)
{
	mHandleThread = (HANDLE)_beginthread(thread_entry, 0, this);
	memset(mInput, 0xCC, kTestBufferSize);
}

IPCHandler::~IPCHandler()
{
	if (!mThread.expired())
	{
		IPC()->disconnect();
		thread()->exit();
	}
}

IPCHandler::HandlerType IPCHandler::connect()
{
	HandlerType type = kTypeClient;
	if (!mIPC->connect(false))
	{
		type = kTypeServer;
		mIPC->connect(true);
	}

	mType = type;
	return type;
}

void IPCHandler::send(const uint32_t count/* = 1*/)
{
	if (mSendCount == 0)
		::GetSystemTimePreciseAsFileTime(&mTimeSendStart);

	EMTIPCWin * ipc = IPC();

	for (uint32_t i = 0; i < count; ++i)
	{
		if (mSendCount >= kTestCount)
			return;

		++mSendCount;

		char * buf = (char *)ipc->alloc(kTestBufferSize);

		memcpy(buf, mInput, sizeof(mInput));

		ipc->send(buf, 0, 0);
	}
}

void IPCHandler::connected()
{
	printf("[EMTIPC] Connected.\n");

	send(kTestSendConcurrent);
}

void IPCHandler::disconnected()
{
	printf("[EMTIPC] Disconnected.\n");

	thread()->exit();
}

void IPCHandler::received(void * buf, const uint64_t uParam0, const uint64_t uParam1)
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
	default:
		send();
	}
}

void IPCHandler::thread_entry(void * arg)
{
	IPCHandler * pThis = (IPCHandler *)arg;
}

static FILETIME s_start;
static FILETIME s_end;

static void my_run(int i, IEMTThread * thread)
{
	if (i == 0)
		::GetSystemTimePreciseAsFileTime(&s_start);
	else if (i == kTestCount - 1)
	{
		::GetSystemTimePreciseAsFileTime(&s_end);

		timeUsage("total: %llu\n", s_start, s_end);

		thread->exit();
	}
}

static void my_entry(void * arg)
{
	IEMTThread * testThread = (IEMTThread *)arg;

	for (int i = 0; i < kTestCount; ++i)
	{
		testThread->queue(createEMTRunnable(std::bind(&my_run, i, testThread)));
	}
}

static int test_queue()
{
	std::shared_ptr<IEMTThread> thread(createEMTThread(), IEMTUnknown_Delete());

	(HANDLE)_beginthread(my_entry, 0, thread.get());

	return thread->exec();
}

class MySemaphore : public IEMTWaitable
{
	EMTIMPL_IEMTUNKNOWN;

public:
	MySemaphore(IEMTThread * thread, HANDLE semaphore);
	~MySemaphore();

protected:
	virtual void run();
	virtual bool isAutoDestroy();

protected:
	virtual void * waitHandle();

private:
	static void releaseSemaphore(void * arg);

private:
	IEMTThread * mThread;
	HANDLE mSemaphore;
	uint32_t mCount;
};

MySemaphore::MySemaphore(IEMTThread * thread, HANDLE semaphore)
	: mThread(thread)
	, mSemaphore(semaphore)
	, mCount(0)
{
	//_beginthread(releaseSemaphore, 0, this);
}

MySemaphore::~MySemaphore()
{
}

void MySemaphore::run()
{
	++mCount;
	if (mCount == 1)
		::GetSystemTimePreciseAsFileTime(&s_start);
	else if (mCount == kTestCount)
	{
		::GetSystemTimePreciseAsFileTime(&s_end);
		timeUsage("total: %llu\n", s_start, s_end);
		mThread->exit();
	}
}

bool MySemaphore::isAutoDestroy()
{
	return false;
}

void * MySemaphore::waitHandle()
{
	return mSemaphore;
}

void MySemaphore::releaseSemaphore(void * arg)
{
	MySemaphore * pThis = (MySemaphore *)arg;

	for (int i = 0; i < kTestCount; ++i)
	{
		::ReleaseSemaphore(pThis->mSemaphore, 1, NULL);
	}
}

static int test_semaphore()
{
	std::shared_ptr<IEMTThread> thread(createEMTThread(), IEMTUnknown_Delete());
	//HANDLE semaphore = ::CreateSemaphore(NULL, kTestCount, kTestCount, NULL);
	//MySemaphore mySem(thread.get(), semaphore);
	HANDLE event = ::CreateEvent(NULL, TRUE, TRUE, NULL);
	MySemaphore mySem(thread.get(), event);

	thread->registerWaitable(&mySem);

	return thread->exec();
}

static int test_pipe()
{
	wchar_t ipcName[] = L"EMTDemo";

	std::shared_ptr<IEMTThread> thread(createEMTThread(), IEMTUnknown_Delete());
	IPCHandler ipcHandler(ipcName, thread);

	IPCHandler::HandlerType type = ipcHandler.connect();
	printf(type == IPCHandler::kTypeServer ? "Server\n" : "Client\n");

	return thread->exec();
}

struct TestPoolContext
{
	HANDLE ev;
	PEMTPOOL pool;
};

static void test_pool_entry(void * arg)
{
	TestPoolContext * ctx = (TestPoolContext *)arg;
	::WaitForSingleObject(ctx->ev, INFINITE);

	for (int i = 0; i < kTestCount; ++i)
	{
		//uint32_t * mem = (uint32_t *)EMTPool_alloc(ctx->pool, kTestBufferSize);
		uint32_t * mem = (uint32_t *)malloc(kTestBufferSize);
		*mem = 0;
		//EMTPool_free(ctx->pool, mem);
		free(mem);
	}
	::GetSystemTimePreciseAsFileTime(&s_end);
}

static int test_pool()
{
	EMTPOOL pool;
	uint32_t metaLen, memLen;
	EMTPool_calcMetaSize(1024 * 1024, kTestBufferSize, &metaLen, &memLen);
	metaLen = (metaLen + (4096 - 1)) & ~(4096 - 1);
	void * mem = malloc(metaLen + memLen);
	memset(mem, 0, metaLen + memLen);
	EMTPool_construct(&pool, 1, 1024 * 1024, kTestBufferSize, 1, mem, (uint8_t *)mem + metaLen);

	TestPoolContext ctx = { ::CreateEvent(NULL, TRUE, FALSE, NULL), &pool };
	HANDLE threads[4];
	for (int i = 0; i < 4; ++i)
	{
		threads[i] = (HANDLE)_beginthread(test_pool_entry, 0, &ctx);
	}

	::Sleep(1000);
	::GetSystemTimePreciseAsFileTime(&s_start);
	::SetEvent(ctx.ev);

	::WaitForMultipleObjects(4, threads, TRUE, INFINITE);

	timeUsage("total: %llu\n", s_start, s_end);

	return 0;
}

int main(int /*argc*/, char* /*argv*/[])
{
	//return test_pipe();
	//return test_queue();
	//return test_semaphore();
	return test_pool();
}
