/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTLINKLIST_H__
#define __EMTLINKLIST_H__

#include <EMTCommon.h>

typedef struct _EMTLINKLISTOPS EMTLINKLISTOPS, * PEMTLINKLISTOPS;
typedef const EMTLINKLISTOPS * PCEMTLINKLISTOPS;
typedef struct _EMTLINKLISTNODE EMTLINKLISTNODE, * PEMTLINKLISTNODE;

struct _EMTLINKLISTOPS
{
	void (*init)(PEMTLINKLISTNODE head);
	void (*prepend)(PEMTLINKLISTNODE head, PEMTLINKLISTNODE node);

	PEMTLINKLISTNODE (*next)(PEMTLINKLISTNODE node);

	PEMTLINKLISTNODE (*take)(PEMTLINKLISTNODE head);
	PEMTLINKLISTNODE (*reverse)(PEMTLINKLISTNODE node);
};

#pragma pack(push, 1)
struct _EMTLINKLISTNODE
{
	volatile int32_t next;
};
#pragma pack(pop)

EXTERN_C PCEMTLINKLISTOPS emtLinkList(void);

#endif // __EMTLINKLIST_H__
