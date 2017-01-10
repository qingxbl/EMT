#define EMTIMPL_LINKLIST
#include "EMTLinkList.h"
#include "EMTPool.h"

static PEMTLINKLISTNODE offsetToNode(PEMTLINKLISTNODE node, const int32_t next)
{
	return (PEMTLINKLISTNODE)((uint8_t *)node + next);
}

static int32_t nodeToOffset(PEMTLINKLISTNODE node, PEMTLINKLISTNODE nextNode)
{
	return (int32_t)((uint8_t *)nextNode - (uint8_t *)node);
}

void EMTLinkList_init(PEMTLINKLISTNODE head)
{
	head->next = 0;
}

void EMTLinkList_prepend(PEMTLINKLISTNODE head, PEMTLINKLISTNODE node)
{
	int32_t headNext;
	const int32_t offset = nodeToOffset(head, node);
	do
	{
		headNext = head->next;
		node->next = headNext ? headNext - offset: 0;
	} while (rt_cmpXchg32(&head->next, offset, headNext) != headNext);
}

PEMTLINKLISTNODE EMTLinkList_next(PEMTLINKLISTNODE node)
{
	PEMTLINKLISTNODE ret = offsetToNode(node, node->next);
	return ret != node ? ret : 0;
}

PEMTLINKLISTNODE EMTLinkList_takeFirst(PEMTLINKLISTNODE head)
{
	int32_t headNext, headNextNext;
	PEMTLINKLISTNODE node = 0;

	do
	{
		node = EMTLinkList_next(head);
		headNext = nodeToOffset(head, node);
		headNextNext = node->next ? headNext + node->next : 0;
	} while (headNext && rt_cmpXchg32(&head->next, headNextNext, headNext) != headNext);

	if (node != head)
		node->next = 0;
	else
		node = 0;

	return node;
}

PEMTLINKLISTNODE EMTLinkList_detach(PEMTLINKLISTNODE head)
{
	int32_t headNext;
	do
	{
		headNext = head->next;
	} while (rt_cmpXchg32(&head->next, 0, headNext) != headNext);

	return headNext ? offsetToNode(head, headNext) : 0;
}

PEMTLINKLISTNODE EMTLinkList_reverse(PEMTLINKLISTNODE node)
{
	PEMTLINKLISTNODE prev = 0;
	PEMTLINKLISTNODE curr = node;
	int32_t prevOffset = 0;

	while (curr)
	{
		PEMTLINKLISTNODE next = EMTLinkList_next(curr);
		const int32_t currOffset = curr->next;
		curr->next = -prevOffset;

		prev = curr;
		prevOffset = currOffset;
		curr = next;
	}

	return prev;
}

PCEMTLINKLISTOPS emtLinkList(void)
{
	static const EMTLINKLISTOPS sOps =
	{
		EMTLinkList_init,
		EMTLinkList_prepend,
		EMTLinkList_next,
		EMTLinkList_takeFirst,
		EMTLinkList_detach,
		EMTLinkList_reverse,
	};

	return &sOps;
}

void EMTLinkList2_init(PEMTLINKLISTNODE2 head)
{
	head->next = 0;
}

void EMTLinkList2_prepend(PEMTLINKLISTNODE2 head, PEMTLINKLISTNODE2 node)
{
	do
	{
		node->next = head->next;
	} while (rt_cmpXchgPtr(&head->next, node, node->next) != node->next);
}

PEMTLINKLISTNODE2 EMTLinkList2_next(PEMTLINKLISTNODE2 node)
{
	return node->next;
}

PEMTLINKLISTNODE2 EMTLinkList2_takeFirst(PEMTLINKLISTNODE2 head)
{
	PEMTLINKLISTNODE2 node = 0;

	do
	{
		node = EMTLinkList2_next(head);
	} while (node && rt_cmpXchgPtr(&head->next, node->next, node) != node);

	if (node != head)
		node->next = 0;
	else
		node = 0;

	return node;
}

PEMTLINKLISTNODE2 EMTLinkList2_detach(PEMTLINKLISTNODE2 head)
{
	PEMTLINKLISTNODE2 headNext;
	do
	{
		headNext = head->next;
	} while (rt_cmpXchgPtr(&head->next, 0, headNext) != headNext);

	return headNext;
}

PEMTLINKLISTNODE2 EMTLinkList2_reverse(PEMTLINKLISTNODE2 node)
{
	PEMTLINKLISTNODE2 prev = 0;
	PEMTLINKLISTNODE2 curr = node;

	while (curr)
	{
		PEMTLINKLISTNODE2 next = EMTLinkList2_next(curr);
		curr->next = prev;

		prev = curr;
		curr = next;
	}

	return prev;
}

PCEMTLINKLISTOPS2 emtLinkList2(void)
{
	static const EMTLINKLISTOPS2 sOps =
	{
		EMTLinkList2_init,
		EMTLinkList2_prepend,
		EMTLinkList2_next,
		EMTLinkList2_takeFirst,
		EMTLinkList2_detach,
		EMTLinkList2_reverse,
	};

	return &sOps;
}
