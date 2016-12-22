#include "stable.h"

#include "EMTShareMemory.h"

#include <Windows.h>

BEGIN_NAMESPACE_ANONYMOUS

class EMTShareMemory : public IEMTShareMemory
{
	EMTIMPL_IEMTUNKNOWN;

public:
	explicit EMTShareMemory(const wchar_t * name);
	virtual ~EMTShareMemory();

protected: // IEMTShareMemory
	virtual uint32_t length();
	virtual void * address();

	virtual void * open(const uint32_t length);
	virtual void close();

private:
	wchar_t * mName;
	uint32_t mLength;
	void * mAddress;
	HANDLE mShareMemory;
};

EMTShareMemory::EMTShareMemory(const wchar_t * name)
	: mName(_wcsdup(name))
	, mLength(0)
	, mAddress(NULL)
	, mShareMemory(NULL)
{

}

EMTShareMemory::~EMTShareMemory()
{
	close();

	free(mName);
}

uint32_t EMTShareMemory::length()
{
	return mLength;
}

void * EMTShareMemory::address()
{
	return mAddress;
}

void * EMTShareMemory::open(const uint32_t length)
{
	do
	{
		if (mShareMemory != NULL)
			break;

		mShareMemory = ::CreateFileMappingW(INVALID_HANDLE_VALUE,
			NULL,
			PAGE_READWRITE,
			0,
			length,
			mName);

		if (mShareMemory == NULL)
			break;

		mAddress = ::MapViewOfFile(mShareMemory, FILE_MAP_ALL_ACCESS, 0, 0, 0);

		mLength = length;
	} while (false);

	return mAddress;
}

void EMTShareMemory::close()
{
	if (mShareMemory == NULL)
		return;

	::UnmapViewOfFile(mAddress);
	::CloseHandle(mShareMemory);

	mAddress = NULL;
	mShareMemory = NULL;
}

END_NAMESPACE_ANONYMOUS

IEMTShareMemory * createEMTShareMemory(const wchar_t * name)
{
	return new EMTShareMemory(name);
}
