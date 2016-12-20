/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTIPC_H__
#define __EMTIPC_H__

#include <stdint.h>
#include <memory>

#include <EMTCommon.h>

struct DECLSPEC_NOVTABLE IEMTIPCSink : public IEMTUnknown
{
	virtual void connected() = 0;
	virtual void disconnected() = 0;

	virtual void received(void * pMem) = 0;
	virtual void called(void * pMem, const uint32_t uContext) = 0;
	virtual void resulted(void * pMem, const uint32_t uContext) = 0;
};

struct IEMTThread;
struct IEMTShareMemory;
class EMTIPCPrivate;
class EMTIPC
{
public:
	enum
	{
		kInvalidConn = ~0U,
	};

public:
	IEMTThread * thread() const;
	IEMTShareMemory * shareMemory() const;
	IEMTIPCSink * sink() const;

	uint32_t connId();
	bool isConnected();

	uint32_t connect(uint32_t uConnId);
	uint32_t disconnect();
	void * alloc(const uint32_t uLen);
	void free(void * pMem);

	void send(void * pMem);
	void call(void * pMem, const uint32_t uContext);
	void result(void * pMem, const uint32_t uContext);

protected:
	explicit EMTIPC(EMTIPCPrivate & dd, IEMTThread * pThread, IEMTShareMemory * pShareMemory, IEMTIPCSink * pSink);
	virtual ~EMTIPC();

private:
	EMTIPC(const EMTIPC &);

protected:
	friend class EMTIPCPrivate;
	std::unique_ptr<EMTIPCPrivate> d_ptr;
};

#endif // __EMTIPC_H__
