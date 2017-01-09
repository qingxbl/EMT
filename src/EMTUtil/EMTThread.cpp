#include "stable.h"

#include "EMTThread.h"

#include <Windows.h>

#include <vector>
#include <algorithm>

BEGIN_NAMESPACE_ANONYMOUS

enum
{
	kInvalidStartPoint = ~0U,
};

class EMTWorkThread : public IEMTThread
{
	EMTIMPL_IEMTUNKNOWN;

public:
	explicit EMTWorkThread();
	virtual ~EMTWorkThread();

protected: // IEMTThread
	virtual bool isCurrentThread();

	virtual uint32_t exec();
	virtual void exit();

	virtual void registerWaitable(IEMTWaitable *waitable);
	virtual void unregisterWaitable(IEMTWaitable *waitable);
	virtual void queue(IEMTRunnable *runnable);
	virtual void delay(IEMTRunnable *runnable, const uint64_t time, const bool repeat);

private:
	static void NTAPI queue_entry(ULONG_PTR Parameter);
	static void APIENTRY timer_entry(LPVOID lpArgToCompletionRoutine, DWORD dwTimerLowValue, DWORD dwTimerHighValue);

	static bool run(IEMTRunnable * runnable);

private:
	DWORD mThreadId;
	HANDLE mThread;

	std::vector<IEMTWaitable *> mRegisteredWaitable;

	bool mRunning;
	uint32_t mStartPoint;
};

EMTWorkThread::EMTWorkThread()
	: mThreadId(::GetCurrentThreadId())
	, mRunning(false)
	, mStartPoint(0)
{
	::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(), &mThread, 0, FALSE, DUPLICATE_SAME_ACCESS);

	mRegisteredWaitable.reserve(MAXIMUM_WAIT_OBJECTS);
}

EMTWorkThread::~EMTWorkThread()
{
	if (mRunning)
	{
		exit();
		if (!isCurrentThread())
			::WaitForSingleObject(mThread, 5000);
	}

	::CloseHandle(mThread);
}

bool EMTWorkThread::isCurrentThread()
{
	return ::GetCurrentThreadId() == mThreadId;
}

uint32_t EMTWorkThread::exec()
{
	if (mRunning)
		return 0;

	mRunning = true;

	HANDLE waitHandles[MAXIMUM_WAIT_OBJECTS << 1];

	while (mRunning)
	{
		const std::size_t count = mRegisteredWaitable.size();

		if (mStartPoint == kInvalidStartPoint)
		{
			std::transform(mRegisteredWaitable.cbegin(), mRegisteredWaitable.cend(), waitHandles, [](IEMTWaitable *c) -> HANDLE { return c->waitHandle(); });
			memcpy(waitHandles + count, waitHandles, sizeof(HANDLE) * count);
			mStartPoint = 0;
		}

		const DWORD rc = MsgWaitForMultipleObjectsEx(count, waitHandles + mStartPoint, INFINITE, QS_ALLEVENTS, MWMO_ALERTABLE);
		const DWORD wokenObject = rc - WAIT_OBJECT_0;
		if (wokenObject >= 0 && wokenObject < count)
		{
			IEMTWaitable * waitable = mRegisteredWaitable[(wokenObject + mStartPoint) % count];
			if (run(waitable))
				unregisterWaitable(waitable);

			if (mStartPoint != kInvalidStartPoint)
				mStartPoint = wokenObject + 1;
		}
		else if (wokenObject == count)
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
			// continue;
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

void EMTWorkThread::exit()
{
	if (!isCurrentThread())
		queue(createEMTRunnable(std::bind(&EMTWorkThread::exit, this)));

	mRunning = false;
}

void EMTWorkThread::registerWaitable(IEMTWaitable * waitable)
{
	if (!isCurrentThread())
		return queue(createEMTRunnable(std::bind(&EMTWorkThread::registerWaitable, this, waitable)));

	if (std::find(mRegisteredWaitable.cbegin(), mRegisteredWaitable.cend(), waitable) == mRegisteredWaitable.cend())
	{
		mRegisteredWaitable.push_back(waitable);
		mStartPoint = kInvalidStartPoint;
	}
}

void EMTWorkThread::unregisterWaitable(IEMTWaitable * waitable)
{
	if (!isCurrentThread())
		return queue(createEMTRunnable(std::bind(&EMTWorkThread::unregisterWaitable, this, waitable)));

	mRegisteredWaitable.erase(std::remove_if(mRegisteredWaitable.begin(), mRegisteredWaitable.end(), [waitable](IEMTWaitable *c) { return c == waitable; }), mRegisteredWaitable.end());
	mStartPoint = kInvalidStartPoint;
}

void EMTWorkThread::queue(IEMTRunnable * runnable)
{
	::QueueUserAPC(&EMTWorkThread::queue_entry, mThread, (ULONG_PTR)(runnable));
}

void EMTWorkThread::delay(IEMTRunnable * runnable, const uint64_t time, const bool repeat)
{

}

void EMTWorkThread::queue_entry(ULONG_PTR Parameter)
{
	run((IEMTRunnable *)Parameter);
}

void EMTWorkThread::timer_entry(LPVOID lpArgToCompletionRoutine, DWORD dwTimerLowValue, DWORD dwTimerHighValue)
{

}

bool EMTWorkThread::run(IEMTRunnable * runnable)
{
	runnable->run();

	const bool ret = runnable->isAutoDestroy();
	if (ret)
		runnable->destruct();

	return ret;
}

END_NAMESPACE_ANONYMOUS

IEMTThread * createEMTThread()
{
	return new EMTWorkThread();
}
