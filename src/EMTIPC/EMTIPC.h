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

	virtual void received(void * pMem, const uint64_t uParam0, const uint64_t uParam1) = 0;
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

	void * alloc(const uint32_t uLen);
	void free(void * pMem);
	uint32_t length(void * pMem);

	uint32_t transfer(void * pMem);
	void * take(const uint32_t uToken);

	void send(void * pMem, const uint64_t uParam0, const uint64_t uParam1);

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
