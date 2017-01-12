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
	void queue_entry();
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
	HANDLE mQueuedEvent;
};

EMTWorkThread::EMTWorkThread()
	: mThreadId(::GetCurrentThreadId())
	, mRunning(false)
	, mStartPoint(0)
	, mQueuedEvent(::CreateEventW(NULL, FALSE, FALSE, NULL))
{
	::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(), &mThread, 0, FALSE, DUPLICATE_SAME_ACCESS);

	mRegisteredWaitable.reserve(MAXIMUM_WAIT_OBJECTS);

	EMTLinkList2_init(&mQueued);

	registerWaitable(createEMTWaitable(std::bind(&EMTWorkThread::queue_entry, this), mQueuedEvent));
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

	bool breakAlertable = false;
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

		const DWORD time = !breakAlertable ? INFINITE : 0;
		const DWORD flags = !breakAlertable ? MWMO_ALERTABLE : 0;
		const DWORD rc = MsgWaitForMultipleObjectsEx(count, waitHandles + mStartPoint, time, QS_ALLEVENTS, flags);
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

		breakAlertable = rc == WAIT_IO_COMPLETION;
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
		::SetEvent(mQueuedEvent);
}

void EMTWorkThread::delay(IEMTRunnable * runnable, const uint64_t time, const bool repeat)
{

}

void EMTWorkThread::queue_entry()
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
