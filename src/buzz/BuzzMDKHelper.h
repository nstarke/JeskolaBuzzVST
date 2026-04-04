#pragma once

// MDK (Machine Development Kit) helper for Buzz machines.
//
// MDK machines inherit from CMDKMachineInterface which overrides Work()
// to perform overlap-add FFT processing. The machine calls
// pCB->GetNearestWaveLevel(-1, -1) during Init to obtain a
// CMDKImplementation object. This object's vtable methods handle:
//
//   [0]  Destructor
//   [6]  Work proxy -> redirects to machine's overlap-add Work (vtable[22])
//   [7]  WorkMonoToStereo proxy
//   [8]  Setup -> calls machine's MDKInit (vtable[23])
//   [10] MDK engine init (called from machine's MDKInit)
//
// Layout matches the real Buzz CMDKImplementation (0x2C bytes):
//   [0]  vtable pointer
//   [1]  machine pointer (back-reference)
//   [2]  subobject pointer
//   ...
//   [10] aligned buffer

#include <windows.h>
#include <cstring>

namespace BuzzVst {

// Create/destroy MDK stub objects
void* CreateMDKStub();
void DestroyMDKStub(void* stub);

// Check if the MDK stub's Init method (vtable[10]) was called,
// confirming the machine is truly MDK.
bool WasMDKInitCalled(void* stub);

// Reset the "Init was called" flag on the MDK stub.
void ResetMDKInitFlag(void* stub);

} // namespace BuzzVst
