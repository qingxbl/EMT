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

typedef struct _EMTLINKLISTOPS2 EMTLINKLISTOPS2, * PEMTLINKLISTOPS2;
typedef const EMTLINKLISTOPS2 * PCEMTLINKLISTOPS2;
typedef struct _EMTLINKLISTNODE2 EMTLINKLISTNODE2, * PEMTLINKLISTNODE2;

struct _EMTLINKLISTOPS2
{
	void(*init)(PEMTLINKLISTNODE2 head);
	void(*prepend)(PEMTLINKLISTNODE2 head, PEMTLINKLISTNODE2 node);

	PEMTLINKLISTNODE2 (*next)(PEMTLINKLISTNODE2 node);

	PEMTLINKLISTNODE2 (*takeFirst)(PEMTLINKLISTNODE2 head);
	PEMTLINKLISTNODE2 (*detach)(PEMTLINKLISTNODE2 head);
	PEMTLINKLISTNODE2 (*reverse)(PEMTLINKLISTNODE2 node);
};

#pragma pack(push, 1)
struct _EMTLINKLISTNODE2
{
	volatile PEMTLINKLISTNODE2 next;
};
#pragma pack(pop)

EXTERN_C PCEMTLINKLISTOPS2 emtLinkList2(void);

#if !defined(USE_VTABLE) || defined(EMTIMPL_LINKLIST)
EMTIMPL_CALL void EMTLinkList2_init(PEMTLINKLISTNODE2 head);
EMTIMPL_CALL void EMTLinkList2_prepend(PEMTLINKLISTNODE2 head, PEMTLINKLISTNODE2 node);
EMTIMPL_CALL PEMTLINKLISTNODE2 EMTLinkList2_next(PEMTLINKLISTNODE2 node);
EMTIMPL_CALL PEMTLINKLISTNODE2 EMTLinkList2_takeFirst(PEMTLINKLISTNODE2 head);
EMTIMPL_CALL PEMTLINKLISTNODE2 EMTLinkList2_detach(PEMTLINKLISTNODE2 head);
EMTIMPL_CALL PEMTLINKLISTNODE2 EMTLinkList2_reverse(PEMTLINKLISTNODE2 node);
#else
#define EMTLinkList2_init emtLinkList2()->init
#define EMTLinkList2_prepend emtLinkList2()->prepend
#define EMTLinkList2_next emtLinkList2()->next
#define EMTLinkList2_takeFirst emtLinkList2()->takeFirst
#define EMTLinkList2_detach emtLinkList2()->detach
#define EMTLinkList2_reverse emtLinkList2()->reverse
#endif

EXTERN_C void * rt_cmpXchgPtr(void * volatile * dest, void * exchg, void * comp);

#endif // __EMTLINKLIST_H__
