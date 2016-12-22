/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTTHREAD_H__
#define __EMTTHREAD_H__

#include <EMTCommon.h>
#include <functional>

struct DECLSPEC_NOVTABLE IEMTRunnable : public IEMTUnknown
{
	virtual void run() = 0;
	virtual bool isAutoDestroy() = 0;
};

struct DECLSPEC_NOVTABLE IEMTWaitable : public IEMTRunnable
{
	virtual void * waitHandle() = 0;
};

struct DECLSPEC_NOVTABLE IEMTThread : public IEMTUnknown
{
	virtual bool isCurrentThread() = 0;

	virtual uint32_t exec() = 0;
	virtual void exit() = 0;

	virtual void registerWaitable(IEMTWaitable *waitable) = 0;
	virtual void unregisterWaitable(IEMTWaitable *waitable) = 0;
	virtual void queue(IEMTRunnable *runnable) = 0;
	virtual void delay(IEMTRunnable *runnable, const uint64_t time, const bool repeat) = 0;
};

IEMTThread * createEMTThread(void);

template <class T>
struct EMTWaitable : public IEMTWaitable, public T
{
	EMTIMPL_IEMTUNKNOWN;

	explicit EMTWaitable(T func, bool autoDestroy) : T(func), autoDestroy(autoDestroy) { }
	explicit EMTWaitable(T func, void * handle, bool autoDestroy) : T(func), handle(handle), autoDestroy(autoDestroy) { }

	virtual void run() { (*this)(); }
	virtual bool isAutoDestroy() { return autoDestroy; }
	virtual void * waitHandle() { return handle; }

	void * handle;
	bool autoDestroy;
};

template <class T>
EMTWaitable<T> * createEMTRunnable(T && func, bool autoDestroy = true)
{
	return new EMTWaitable<T>(std::forward<T>(func), autoDestroy);
}

template <class T>
EMTWaitable<T> * createEMTWaitable(T && func, void * handle, bool autoDestroy = false)
{
	return new EMTWaitable<T>(std::forward<T>(func), handle, autoDestroy);
}

#endif // __EMTTHREAD_H__
