#include "EMTThread.h"

#include <Windows.h>

#include <vector>
#include <algorithm>

BEGIN_NAMESPACE_ANONYMOUS

class EMTWorkThread : public IEMTThread
{
	IMPL_IEMTUNKNOWN;

public:
	explicit EMTWorkThread();
	virtual ~EMTWorkThread();

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

EMTWorkThread::EMTWorkThread()
{
	mThread = ::CreateThread(NULL, 0, &EMTWorkThread::thread_entry, this, 0, &mThreadId);

	mRegisteredWaitable.reserve(MAXIMUM_WAIT_OBJECTS);
}

EMTWorkThread::~EMTWorkThread()
{
	if (mRunning)
	{
		exit();
		::WaitForSingleObject(mThread, 5000);
	}

	::CloseHandle(mThread);
}

void EMTWorkThread::registerWaitable(IEMTWaitable * waitable)
{
	if (!isCurrentThread())
		return queue(createEMTRunnable(std::bind(&EMTWorkThread::registerWaitable, this, waitable)), 0);

	if (std::find(mRegisteredWaitable.cbegin(), mRegisteredWaitable.cend(), waitable) == mRegisteredWaitable.cend())
	{
		mRegisteredWaitable.push_back(waitable);
		rebuildRegisteredHandles();
	}
}

void EMTWorkThread::unregisterWaitable(IEMTWaitable * waitable)
{
	if (!isCurrentThread())
		return queue(createEMTRunnable(std::bind(&EMTWorkThread::unregisterWaitable, this, waitable)), 0);

	mRegisteredWaitable.erase(std::remove_if(mRegisteredWaitable.begin(), mRegisteredWaitable.end(), [waitable](IEMTWaitable *c) { return c == waitable; }), mRegisteredWaitable.end());
	rebuildRegisteredHandles();
}

void EMTWorkThread::queue(IEMTRunnable * runnable, uint32_t delay)
{
	::QueueUserAPC(&EMTWorkThread::queue_entry, mThread, (ULONG_PTR)(runnable));
}

bool EMTWorkThread::isCurrentThread()
{
	return ::GetCurrentThreadId() == mThreadId;
}

void EMTWorkThread::exit()
{
	if (!isCurrentThread())
		return queue(createEMTRunnable(std::bind(&EMTWorkThread::exit, this)), 0);

	mRunning = false;
}

DWORD EMTWorkThread::run()
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

void EMTWorkThread::rebuildRegisteredHandles()
{
	std::transform(mRegisteredWaitable.cbegin(), mRegisteredWaitable.cend(), mRegsiteredHandles, [](IEMTWaitable *c) -> HANDLE { return c->waitHandle(); });
}

DWORD EMTWorkThread::thread_entry(LPVOID lpThreadParameter)
{
	return static_cast<EMTWorkThread *>(lpThreadParameter)->run();
}

void EMTWorkThread::queue_entry(ULONG_PTR Parameter)
{
	IEMTRunnable * runnable = (IEMTRunnable *)Parameter;
	runnable->run();

	if (runnable->isAutoDestroy())
		runnable->destruct();
}

END_NAMESPACE_ANONYMOUS

IEMTThread * createEMTThread()
{
	return new EMTWorkThread();
}
