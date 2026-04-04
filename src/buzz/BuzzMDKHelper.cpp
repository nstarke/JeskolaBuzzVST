#include "BuzzMDKHelper.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace BuzzVst {

// __thiscall no-op stubs. MSVC __thiscall passes 'this' in ECX.
// __fastcall is the closest we can use in static functions (ECX + EDX).
static void __fastcall StubNoOp(void*, void*) {}
static void __fastcall StubNoOp1(void*, void*, void*) {}
static int  __fastcall StubRetZero(void*, void*) { return 0; }
static int  __fastcall StubRetZero1(void*, void*, void*) { return 0; }
static int  __fastcall StubRetFalse(void*, void*) { return 0; }
static int  __fastcall StubRetTrue(void*, void*) { return 1; }

// The MDK vtable. 32 entries to cover any method the machine might call.
static void* g_mdkVtable[32] = { nullptr };
static bool g_vtableInit = false;

static void InitVtable() {
	if (g_vtableInit) return;
	// Fill all entries with a safe no-op
	for (int i = 0; i < 32; i++) {
		g_mdkVtable[i] = (void*)StubNoOp1; // accepts 1 arg safely
	}
	// Override specific entries as needed:
	// [8]  offset 0x20 - called with 1 arg (0xffffffff)
	// [10] offset 0x28 - MDKInit(CMachineDataInput*)
	g_mdkVtable[8] = (void*)StubNoOp1;
	g_mdkVtable[10] = (void*)StubNoOp1;
	g_vtableInit = true;
}

// MDK stub object layout (must be at least 0x2C = 44 bytes):
// [0]  = vtable pointer
// [1]  = machine pointer (set by machine to point back to itself)
// [2-10] = other fields
struct MDKStubObject {
	void** vptr;          // offset 0x00
	void* machinePtr;     // offset 0x04
	void* field2;         // offset 0x08
	int field3;           // offset 0x0C
	int field4;           // offset 0x10
	int field5;           // offset 0x14
	int field6;           // offset 0x18
	int field7;           // offset 0x1C
	int field8;           // offset 0x20
	int field9;           // offset 0x24
	void* alignedBuf;     // offset 0x28
	char extra[64];       // extra padding
};

void* CreateMDKStub() {
	InitVtable();
	auto* stub = (MDKStubObject*)calloc(1, sizeof(MDKStubObject));
	if (!stub) return nullptr;
	stub->vptr = g_mdkVtable;

	char dbg[128];
	snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] Created MDK stub at %p vtable=%p\n",
		(void*)stub, (void*)g_mdkVtable);
	OutputDebugStringA(dbg);

	return stub;
}

void DestroyMDKStub(void* stub) {
	if (stub) free(stub);
}

} // namespace BuzzVst
