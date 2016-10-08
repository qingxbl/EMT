#include <windows.h>
#include <EMTPool/EMTPool.h>

int run(void)
{
	while (true)
	{
		const DWORD rc = MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLEVENTS, MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
		if (rc == WAIT_OBJECT_0 + 0)
		{
			MSG msg;
			while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}
		else if (rc == WAIT_IO_COMPLETION)
		{
			continue;
		}
		else if (rc == WAIT_FAILED)
		{
			// assert(false);
		}
		else
		{
			// assert(false);
		}
	}

	return 0;
}


int main(int /*argc*/, char* /*argv*/[])
{
	SetEnvironmentVariableW(L"pipeName", L"8f752ba4-0a88-48d7-bdfc-08b1dc2162a5");

	HMODULE hModule = ::LoadLibraryExW(L"gpu_accelerate.dll", NULL, LOAD_WITH_ALTERED_SEARCH_PATH);

	typedef void(*PFN_ONLOAD)(void);
	PFN_ONLOAD pfn_onLoad = (PFN_ONLOAD)::GetProcAddress(hModule, "onLoad");
	pfn_onLoad();

	return run();
}

EXTERN_C void * rt_memset(void *mem, const int val, const uint32_t size) { return memset(mem, val, size); }
EXTERN_C uint16_t rt_cmpXchg16(volatile uint16_t *dest, uint16_t exchg, uint16_t comp) { return (uint16_t)InterlockedCompareExchange16((volatile SHORT *)dest, (SHORT)exchg, (SHORT)comp); }
EXTERN_C uint32_t rt_cmpXchg32(volatile uint32_t *dest, uint32_t exchg, uint32_t comp) { return (uint32_t)InterlockedCompareExchange((volatile LONG *)dest, (LONG)exchg, (LONG)comp); }
