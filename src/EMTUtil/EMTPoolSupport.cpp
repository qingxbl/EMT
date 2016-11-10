#include "stable.h"

#include "EMTPoolSupport.h"

#include <EMTUtil/EMTPool.h>

#include <Windows.h>

#include <vector>
#include <map>
#include <memory>

BEGIN_NAMESPACE_ANONYMOUS

struct EMTPOOL_Delete
{
	void operator()(EMTPOOL * p) const
	{
		destructEMTPool(p);
		delete p;
	}
};

class EMTMultiPool : public IEMTMultiPool
{
	IMPL_IEMTUNKNOWN;

	struct Pool
	{
		std::unique_ptr<EMTPOOL, EMTPOOL_Delete> pool;

		uint32_t length;
		uint32_t blockLength;
		uint32_t blockLimit;

		void * addressStart;
		void * addressEnd;
	};

	struct Meta
	{
		volatile uint32_t nextId;
	};

	struct BlockMeta
	{
		uint32_t allocLen;
	};

	struct PartialMeta
	{
		uint32_t totalLen;
		uint32_t slotWriter;
		uint32_t slotReader;
	};

public:
	explicit EMTMultiPool();
	virtual ~EMTMultiPool();

protected: // IEMTMultiPool
	virtual void addConfig(const uint32_t length, const uint32_t blockLength, const uint32_t blockLimit);
	virtual void init(void * p);

	virtual uint32_t id();

	virtual void * alloc(const uint32_t memLength);
	virtual void free(void * mem);
	virtual void freeAll(const uint32_t id);

	virtual const bool transfer(void * mem, const uint32_t toId, uint32_t * token, uint32_t * poolId);
	virtual void transferPartial(void * mem, const uint32_t toId, uint32_t * token, uint32_t * poolId);
	virtual void * take(const uint32_t token, const uint32_t poolId);

private:
	void * takePartial(const uint32_t token, const uint32_t poolId);

	Pool & selectByLength(const uint32_t length);
	Pool & selectByAddress(void * address);

private:
	uint32_t mId;
	Meta * mMeta;

	uint32_t mBlockLimit;
	void * mAddressStart;
	void * mAddressEnd;

	std::vector<Pool> mPools;
	std::map<void *, BlockMeta> mBlocks;
};

EMTMultiPool::EMTMultiPool()
	: mId(0)
{
}

EMTMultiPool::~EMTMultiPool()
{
}

void EMTMultiPool::addConfig(const uint32_t length, const uint32_t blockLength, const uint32_t blockLimit)
{
	if (!mId)
		return;

	mPools.push_back(std::move(Pool{ nullptr, length, blockLength, blockLimit }));
}

void EMTMultiPool::init(void * p)
{
	if (mId)
		return;

	mMeta = (Meta *)p;
	mId = ::InterlockedIncrement(&mMeta->nextId);

	uint32_t metaOffset = sizeof(*mMeta);
	mAddressStart = p;
	mAddressEnd = p;

	for (Pool & pool : mPools)
	{
		EMTPOOL * p = new EMTPOOL;
		pool.pool.reset(p);
		pool.addressStart = mAddressEnd;
		pool.addressEnd = mAddressEnd = (uint8_t *)pool.addressStart + pool.length;
		constructEMTPool(p, mId, pool.addressStart, metaOffset, pool.length, pool.blockLength);

		metaOffset = 0;
		mBlockLimit = pool.blockLimit;
	}

}

uint32_t EMTMultiPool::id()
{
	return mId;
}

void * EMTMultiPool::alloc(const uint32_t memLength)
{
	if (!mId)
		return nullptr;

	if (memLength > mBlockLimit)
	{
		void * mem = malloc(memLength);
		mBlocks.insert(std::make_pair(mem, std::move(BlockMeta{ memLength })));
		return mem;
	}

	EMTPOOL * p = selectByLength(memLength).pool.get();
	return p->alloc(p, memLength);
}

void EMTMultiPool::free(void * mem)
{
	if (!mId)
		return;

	if (mem < mAddressStart || mem >= mAddressEnd)
	{
		mBlocks.erase(mem);
		free(mem);
	}

	EMTPOOL * p = selectByAddress(mem).pool.get();
	return p->free(p, mem);
}

void EMTMultiPool::freeAll(const uint32_t id)
{
	if (!mId)
		return;

	std::for_each(mPools.begin(), mPools.end(), [id](const Pool & p) { p.pool->freeAll(p.pool.get(), id); });
}

const bool EMTMultiPool::transfer(void * mem, const uint32_t toId, uint32_t * token, uint32_t * poolId)
{
	EMTPOOL * p = nullptr;
	void * transferMem = mem;
	if (mem >= mAddressStart && mem < mAddressEnd)
	{
		Pool & pool = selectByAddress(mem);
		p = pool.pool.get();

		*poolId = std::distance(&mPools.front(), &pool);
	}
	else
	{
		Pool & pool = mPools.front();
		p = pool.pool.get();

		transferMem = p->alloc(p, pool.blockLength);
		*poolId = mPools.size();

		PartialMeta * partialMeta = (PartialMeta *)transferMem;
		const uint32_t slots = (pool.blockLength - sizeof(*partialMeta)) / sizeof(uint32_t);
		partialMeta->totalLen = mBlocks[mem].allocLen;
		partialMeta->slotWriter = 0;
		partialMeta->slotReader = slots - 1;
	}

	*token = p->transfer(p, transferMem, toId);
	return transferMem != mem;
}

void EMTMultiPool::transferPartial(void * mem, const uint32_t toId, uint32_t * token, uint32_t * poolId)
{
	PartialMeta * partialMeta = (PartialMeta *)take(*token, 0);

	Pool & pool = mPools.back();
	EMTPOOL * p = pool.pool.get();
	const uint32_t slots = (pool.blockLength - sizeof(*partialMeta)) / sizeof(uint32_t);
	uint32_t * slot = (uint32_t *)(partialMeta + 1);

	for (uint8_t * cur = (uint8_t *)mem, * end = cur + partialMeta->totalLen; cur < end; cur += pool.blockLength)
	{
		void * buf = p->alloc(p, pool.blockLength);
		memcpy(buf, cur, pool.blockLength);

		::InterlockedExchange(slot + partialMeta->slotWriter, p->transfer(p, mem, toId));

		const uint32_t slotWriterNext = partialMeta->slotWriter + 1 < slots ? partialMeta->slotWriter + 1 : 0;
		while (partialMeta->slotReader == slotWriterNext);
		::InterlockedExchange(&partialMeta->slotWriter, slotWriterNext);
	}
}

void * EMTMultiPool::take(const uint32_t token, const uint32_t poolId)
{
	if (poolId >= mPools.size())
		return takePartial(token, 0);

	EMTPOOL * p = mPools[poolId].pool.get();
	return p->take(p, token);
}

void * EMTMultiPool::takePartial(const uint32_t token, const uint32_t poolId)
{
	PartialMeta * partialMeta = (PartialMeta *)take(token, poolId);

	Pool & pool = mPools.back();
	EMTPOOL * p = pool.pool.get();
	const uint32_t slots = (pool.blockLength - sizeof(*partialMeta)) / sizeof(uint32_t);
	uint32_t * slot = (uint32_t *)(partialMeta + 1);

	void * mem = alloc(partialMeta->totalLen);

	for (uint8_t * cur = (uint8_t *)mem, *end = cur + partialMeta->totalLen; cur < end; cur += pool.blockLength)
	{
		const uint32_t slotReaderNext = partialMeta->slotReader + 1 < slots ? partialMeta->slotReader + 1 : 0;
		while (partialMeta->slotWriter == slotReaderNext);
		::InterlockedExchange(&partialMeta->slotReader, slotReaderNext);

		void * buf = p->take(p, slot[partialMeta->slotReader]);
		memcpy(cur, buf, pool.blockLength);

		p->free(p, buf);
	}

	free(partialMeta);

	return mem;
}

EMTMultiPool::Pool & EMTMultiPool::selectByLength(const uint32_t length)
{
	return *std::find_if(mPools.begin(), mPools.end(), [length](const Pool & p) { return length <= p.blockLimit; });
}

EMTMultiPool::Pool & EMTMultiPool::selectByAddress(void * address)
{
	return *std::find_if(mPools.begin(), mPools.end(), [address](const Pool & p) { return address >= p.addressStart && address < p.addressEnd; });
}

END_NAMESPACE_ANONYMOUS

IEMTMultiPool * createEMTMultiPool()
{
	return new EMTMultiPool();
}

EXTERN_C void * rt_memset(void *mem, const int val, const uint32_t size) { return memset(mem, val, size); }
EXTERN_C uint16_t rt_cmpXchg16(volatile uint16_t *dest, uint16_t exchg, uint16_t comp) { return (uint16_t)::InterlockedCompareExchange16((volatile SHORT *)dest, (SHORT)exchg, (SHORT)comp); }
EXTERN_C uint32_t rt_cmpXchg32(volatile uint32_t *dest, uint32_t exchg, uint32_t comp) { return (uint32_t)::InterlockedCompareExchange((volatile LONG *)dest, (LONG)exchg, (LONG)comp); }
