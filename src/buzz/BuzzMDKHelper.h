#pragma once

// Stub MDK (Machine Development Kit) helper for Buzz machines.
// MDK machines call GetNearestWaveLevel(-1, -1) to get this interface.
// We provide a heap-allocated object with a raw vtable of no-op stubs.

#include <windows.h>
#include <cstring>

namespace BuzzVst {

// Forward declare the no-op functions (defined in BuzzMDKHelper.cpp)
void* CreateMDKStub();
void DestroyMDKStub(void* stub);

} // namespace BuzzVst
