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

	PEMTLINKLISTNODE (*takeFirst)(PEMTLINKLISTNODE head);
	PEMTLINKLISTNODE (*detach)(PEMTLINKLISTNODE head);
	PEMTLINKLISTNODE (*reverse)(PEMTLINKLISTNODE node);
};

#pragma pack(push, 1)
struct _EMTLINKLISTNODE
{
	volatile int32_t next;
};
#pragma pack(pop)

EXTERN_C PCEMTLINKLISTOPS emtLinkList(void);

#if !defined(USE_VTABLE) || defined(EMTIMPL_LINKLIST)
EMTIMPL_CALL void EMTLinkList_init(PEMTLINKLISTNODE head);
EMTIMPL_CALL void EMTLinkList_prepend(PEMTLINKLISTNODE head, PEMTLINKLISTNODE node);
EMTIMPL_CALL PEMTLINKLISTNODE EMTLinkList_next(PEMTLINKLISTNODE node);
EMTIMPL_CALL PEMTLINKLISTNODE EMTLinkList_takeFirst(PEMTLINKLISTNODE head);
EMTIMPL_CALL PEMTLINKLISTNODE EMTLinkList_detach(PEMTLINKLISTNODE head);
EMTIMPL_CALL PEMTLINKLISTNODE EMTLinkList_reverse(PEMTLINKLISTNODE node);
#else
#define EMTLinkList_init emtLinkList()->init
#define EMTLinkList_prepend emtLinkList()->prepend
#define EMTLinkList_next emtLinkList()->next
#define EMTLinkList_takeFirst emtLinkList()->takeFirst
#define EMTLinkList_detach emtLinkList()->detach
#define EMTLinkList_reverse emtLinkList()->reverse
#endif

#endif // __EMTLINKLIST_H__
