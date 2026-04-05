#include <windows.h>
#include <cstring>
#include "TestFramework.h"

// Patch MessageBoxA/W to no-ops so machine DLLs that pop up dialogs
// (e.g. "unregistered" nag screens) don't block the test suite.
static void PatchMessageBoxes() {
	// x86 stub: mov eax, 1; ret 16  (IDOK, stdcall 4 args)
	static const unsigned char stub[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC2, 0x10, 0x00 };

	HMODULE hUser32 = GetModuleHandleA("user32.dll");
	if (!hUser32) hUser32 = LoadLibraryA("user32.dll");
	if (!hUser32) return;

	const char* funcs[] = { "MessageBoxA", "MessageBoxW" };
	for (auto* name : funcs) {
		void* addr = (void*)GetProcAddress(hUser32, name);
		if (!addr) continue;
		DWORD oldProtect = 0;
		if (VirtualProtect(addr, sizeof(stub), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			memcpy(addr, stub, sizeof(stub));
			VirtualProtect(addr, sizeof(stub), oldProtect, &oldProtect);
		}
	}
}

int main(int argc, char* argv[])
{
	PatchMessageBoxes();

	printf("BuzzBridge Test Suite\n");
	printf("========================================\n\n");

	return TestFW::RunAllTests();
}
