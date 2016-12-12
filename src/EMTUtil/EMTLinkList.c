#include "EMTLinkList.h"
#include "EMTPool.h"

static PEMTLINKLISTNODE offsetToNode(PEMTLINKLISTNODE node, const int32_t next)
{
	return (PEMTLINKLISTNODE)((uint8_t *)node + next);
}

static int32_t nodeToOffset(PEMTLINKLISTNODE node, PEMTLINKLISTNODE nextNode)
{
	return (uint8_t *)nextNode - (uint8_t *)node;
}

static void EMTLinkList_init(PEMTLINKLISTNODE head)
{
	head->next = 0;
}

static void EMTLinkList_prepend(PEMTLINKLISTNODE head, PEMTLINKLISTNODE node)
{
	int32_t headNext;
	const int32_t offset = nodeToOffset(head, node);
	do
	{
		headNext = head->next;
		node->next = headNext - offset;
	} while (rt_cmpXchg32(&head->next, offset, headNext) != headNext);
}

static PEMTLINKLISTNODE EMTLinkList_next(PEMTLINKLISTNODE node)
{
	PEMTLINKLISTNODE ret = offsetToNode(node, node->next);
	return ret != node ? ret : 0;
}

static PEMTLINKLISTNODE EMTLinkList_take(PEMTLINKLISTNODE head)
{
	int32_t headNext;
	do
	{
		headNext = head->next;
	} while (rt_cmpXchg32(&head->next, 0, headNext) != headNext);

	return headNext ? offsetToNode(head, headNext) : 0;
}

static PEMTLINKLISTNODE EMTLinkList_reverse(PEMTLINKLISTNODE node)
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
		EMTLinkList_take,
		EMTLinkList_reverse,
	};

	return &sOps;
}
