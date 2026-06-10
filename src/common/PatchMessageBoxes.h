#pragma once

#include <windows.h>
#include <cstring>

namespace BuzzVst {

// Patch MessageBoxA/W to no-ops so machine DLLs that pop up dialogs
// (e.g. "unregistered" nag screens) don't block the process.
// Overwrites the first bytes of each function in user32.dll with a stub
// that returns IDOK (1) immediately.
inline void PatchMessageBoxes() {
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

// Patch MessageBoxA/W exactly once per process. Safe to call from any of the
// machine-DLL entry points (scan, load, controller info-load); the first call
// installs the no-op stub and the rest are cheap no-ops. Uses a C++11 magic
// static so initialization is thread-safe.
inline void PatchMessageBoxesOnce() {
    static bool done = (PatchMessageBoxes(), true);
    (void)done;
}

} // namespace BuzzVst
