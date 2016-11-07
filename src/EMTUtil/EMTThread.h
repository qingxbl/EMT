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
struct EMTRunnable : public IEMTRunnable, public T
{
	IMPL_IEMTUNKNOWN;

	explicit EMTRunnable(T func, bool autoDestroy) : T(func), autoDestroy(autoDestroy) { }

	virtual void run() { (*this)(); }
	virtual bool isAutoDestroy() { return autoDestroy; }

	bool autoDestroy;
};

template <class T>
EMTRunnable<T> * createEMTRunnable(T && func, bool autoDestroy = true)
{
	return new EMTRunnable<T>(std::forward<T>(func), autoDestroy);
}

#endif // __EMTTHREAD_H__
