/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTIPCWIN_H__
#define __EMTIPCWIN_H__

#include "EMTIPC.h"

class EMTIPCWinPrivate;
class EMTIPCWin : public EMTIPC
{
public:
	explicit EMTIPCWin(const wchar_t * pName, IEMTThread * pThread, IEMTIPCSink * pSink);
	virtual ~EMTIPCWin();

	bool connect(bool isServer);
	void disconnect();

private:
	friend class EMTIPCWinPrivate;
};

#endif // __EMTIPCWIN_H__
