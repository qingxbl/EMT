#include "EMTThread.h"

#include <Windows.h>

#include <vector>
#include <algorithm>

BEGIN_NAMESPACE_ANONYMOUS

class WorkThread : public IEMTThread
{
	IMPL_IEMTUNKNOWN;

public:
	explicit WorkThread();
	virtual ~WorkThread();

protected: // IEMTThread
	virtual void registerWaitable(IEMTWaitable *waitable);
	virtual void unregisterWaitable(IEMTWaitable *waitable);
	virtual void queue(IEMTRunnable *runnable, uint32_t delay);
	virtual bool isCurrentThread();
	virtual void exit();

private:
	DWORD run();
	void rebuildRegisteredHandles();

private:
	static DWORD WINAPI thread_entry(LPVOID lpThreadParameter);
	static void NTAPI queue_entry(ULONG_PTR Parameter);

private:
	DWORD mThreadId;
	HANDLE mThread;

	HANDLE mRegsiteredHandles[MAXIMUM_WAIT_OBJECTS];
	std::vector<IEMTWaitable *> mRegisteredWaitable;

	bool mRunning;
};

WorkThread::WorkThread()
{
	mThread = ::CreateThread(NULL, 0, &WorkThread::thread_entry, this, 0, &mThreadId);

	mRegisteredWaitable.reserve(MAXIMUM_WAIT_OBJECTS);
}

WorkThread::~WorkThread()
{
	if (mRunning)
	{
		exit();
		::WaitForSingleObject(mThread, 5000);
	}

	CloseHandle(mThread);
}

void WorkThread::registerWaitable(IEMTWaitable * waitable)
{
	if (!isCurrentThread())
		return queue(createEMTRunnable(std::bind(&WorkThread::registerWaitable, this, waitable)), 0);

	if (std::find(mRegisteredWaitable.cbegin(), mRegisteredWaitable.cend(), waitable) == mRegisteredWaitable.cend())
	{
		mRegisteredWaitable.push_back(waitable);
		rebuildRegisteredHandles();
	}
}

void WorkThread::unregisterWaitable(IEMTWaitable * waitable)
{
	if (!isCurrentThread())
		return queue(createEMTRunnable(std::bind(&WorkThread::unregisterWaitable, this, waitable)), 0);

	mRegisteredWaitable.erase(std::remove_if(mRegisteredWaitable.begin(), mRegisteredWaitable.end(), [waitable](IEMTWaitable *c) { return c == waitable; }), mRegisteredWaitable.end());
	rebuildRegisteredHandles();
}

void WorkThread::queue(IEMTRunnable * runnable, uint32_t delay)
{
	::QueueUserAPC(&WorkThread::queue_entry, mThread, (ULONG_PTR)(runnable));
}

bool WorkThread::isCurrentThread()
{
	return ::GetCurrentThreadId() == mThreadId;
}

void WorkThread::exit()
{
	if (!isCurrentThread())
		return queue(createEMTRunnable(std::bind(&WorkThread::exit, this)), 0);

	mRunning = false;
}

DWORD WorkThread::run()
{
	mRunning = true;

	while (mRunning)
	{
		const std::size_t count = mRegisteredWaitable.size();
		const DWORD rc = MsgWaitForMultipleObjectsEx(count, mRegsiteredHandles, INFINITE, QS_ALLEVENTS, MWMO_ALERTABLE);
		if (rc >= WAIT_OBJECT_0 && rc < mRegisteredWaitable.size())
		{
			const DWORD wakeup = rc - WAIT_OBJECT_0;
			IEMTWaitable * waitable = mRegisteredWaitable[wakeup];
			waitable->run();
			if (std::find(mRegisteredWaitable.cbegin(), mRegisteredWaitable.cend(), waitable) != mRegisteredWaitable.cend())
			{
				PHANDLE handleWakeup = mRegsiteredHandles + wakeup;
				PHANDLE handleEnd = mRegsiteredHandles + mRegisteredWaitable.size();
				*handleWakeup = INVALID_HANDLE_VALUE;
				std::remove(handleWakeup, handleEnd, INVALID_HANDLE_VALUE);
				*(handleEnd - 1) = waitable->waitHandle();
			}
		}
		else if (rc == WAIT_OBJECT_0 + count)
		{
			MSG msg;
			while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}
		else if (rc == WAIT_IO_COMPLETION)
		{
			continue;
		}
		else if (rc == WAIT_FAILED)
		{
			// assert(false);
		}
		else
		{
			// assert(false);
		}
	}

	return 0;
}

void WorkThread::rebuildRegisteredHandles()
{
	std::transform(mRegisteredWaitable.cbegin(), mRegisteredWaitable.cend(), mRegsiteredHandles, [](IEMTWaitable *c) -> HANDLE { return c->waitHandle(); });
}

DWORD WorkThread::thread_entry(LPVOID lpThreadParameter)
{
	return static_cast<WorkThread *>(lpThreadParameter)->run();
}

void WorkThread::queue_entry(ULONG_PTR Parameter)
{
	IEMTRunnable * runnable = (IEMTRunnable *)Parameter;
	runnable->run();

	if (runnable->isAutoDestroy())
		runnable->destruct();
}

END_NAMESPACE_ANONYMOUS

IEMTThread * createEMTThread()
{
	return new WorkThread();
}
