#include "stable.h"

#include "EMTThread.h"

#include <EMTUtil/EMTLinkList.h>

#include <Windows.h>

#include <vector>
#include <algorithm>

BEGIN_NAMESPACE_ANONYMOUS

enum
{
	kInvalidStartPoint = ~0U,

	kWaitType_None = 0,
	kWaitType_Object = 1 << 0,
	kWaitType_Alertable = 1 << 1,
	kWaitType_Msg = 1 << 2,
	kWaitType_All = kWaitType_Object + kWaitType_Alertable + kWaitType_Msg,
};

class EMTWorkThread : public IEMTThread
{
	EMTIMPL_IEMTUNKNOWN;

	struct QueuedItem
	{
		EMTLINKLISTNODE2 node;
		IEMTRunnable * runnable;

		QueuedItem(IEMTRunnable * runnable) : runnable(runnable) { }
	};

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
	void runQueued();

	static void NTAPI queue_entry(ULONG_PTR Parameter);
	static void APIENTRY timer_entry(LPVOID lpArgToCompletionRoutine, DWORD dwTimerLowValue, DWORD dwTimerHighValue);

	static bool run(IEMTRunnable * runnable);

private:
	DWORD mThreadId;
	HANDLE mThread;

	std::vector<IEMTWaitable *> mRegisteredWaitable;
	std::vector<IEMTRunnable *> mRunnable;

	bool mRunning;
	uint32_t mStartPoint;

	EMTLINKLISTNODE2 mQueued;
};

EMTWorkThread::EMTWorkThread()
	: mThreadId(::GetCurrentThreadId())
	, mRunning(false)
	, mStartPoint(0)
{
	::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(), &mThread, 0, FALSE, DUPLICATE_SAME_ACCESS);

	mRegisteredWaitable.reserve(MAXIMUM_WAIT_OBJECTS);

	EMTLinkList2_init(&mQueued);
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

	uint32_t waitType = kWaitType_All;
	HANDLE waitHandles[MAXIMUM_WAIT_OBJECTS << 1];

	while (mRunning)
	{
		const DWORD count = (DWORD)mRegisteredWaitable.size();

		if (mStartPoint == kInvalidStartPoint)
		{
			std::transform(mRegisteredWaitable.cbegin(), mRegisteredWaitable.cend(), waitHandles, [](IEMTWaitable *c) -> HANDLE { return c->waitHandle(); });
			memcpy(waitHandles + count, waitHandles, sizeof(HANDLE) * count);
			mStartPoint = 0;
		}

		const DWORD milliseconds = waitType == kWaitType_All ? INFINITE : 0;
		const DWORD waitCount = waitType & kWaitType_Object ? count : 0;
		const DWORD wakeMask = waitType & kWaitType_Msg ? QS_ALLEVENTS : 0;
		const DWORD flags = waitType & kWaitType_Alertable ? MWMO_ALERTABLE : 0;
		const DWORD rc = ::MsgWaitForMultipleObjectsEx(waitCount, waitHandles + mStartPoint, milliseconds, wakeMask, flags);
		const DWORD wokenObject = rc - WAIT_OBJECT_0;
		if (wokenObject >= 0 && wokenObject < waitCount)
		{
			IEMTWaitable * waitable = mRegisteredWaitable[(wokenObject + mStartPoint) % count];
			if (run(waitable))
				unregisterWaitable(waitable);

			if (mStartPoint != kInvalidStartPoint)
				mStartPoint = wokenObject + 1;

			waitType &= ~kWaitType_Object;
		}
		else if (wokenObject == waitCount)
		{
			MSG msg;
			while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}

			waitType &= ~kWaitType_Msg;
		}
		else if (rc == WAIT_IO_COMPLETION)
		{
			runQueued();
			waitType &= ~kWaitType_Alertable;
			// continue;
		}
		else if (rc == WAIT_TIMEOUT)
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

		if (rc == WAIT_TIMEOUT || waitType == kWaitType_None)
			waitType = kWaitType_All;
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
	if (EMTLinkList2_prepend(&mQueued, &(new QueuedItem(runnable))->node) == NULL)
		::QueueUserAPC(EMTWorkThread::queue_entry, mThread, NULL);
}

void EMTWorkThread::delay(IEMTRunnable * runnable, const uint64_t time, const bool repeat)
{

}

void EMTWorkThread::runQueued()
{
	EMTLINKLISTNODE2 * node = EMTLinkList2_detach(&mQueued);
	node = EMTLinkList2_reverse(node);

	while (node)
	{
		QueuedItem * q = (QueuedItem *)node;
		node = EMTLinkList2_next(node);

		run(q->runnable);
		delete q;
	}
}

void EMTWorkThread::queue_entry(ULONG_PTR Parameter)
{

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
