#include "BuzzMDKHelper.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <malloc.h>

namespace BuzzVst {

// MDK stub object layout (0x2C = 44 bytes, matching real CMDKImplementation
// from FUN_00424f10 in the real Buzz host):
struct MDKStubObject {
	void** vptr;          // offset 0x00 - vtable pointer
	void* machinePtr;     // offset 0x04 - back-pointer to the machine
	void* subObject;      // offset 0x08 - internal subobject (0x28 bytes)
	int field3;           // offset 0x0C
	int field4;           // offset 0x10
	int field5;           // offset 0x14
	int field6;           // offset 0x18
	int field7;           // offset 0x1C
	int field8;           // offset 0x20
	int field9;           // offset 0x24
	void* alignedBuf;     // offset 0x28
	// Extra field past the real Buzz layout, for our own tracking:
	int initCalled;       // offset 0x2C - set to 1 when vtable[10] is called
};

// __thiscall no-op stubs. MSVC __thiscall passes 'this' in ECX.
// __fastcall is the closest portable substitute (ECX + EDX).
static void __fastcall StubNoOp1(void*, void*, void*) {}
static void __fastcall StubNoOp2(void*, void*, void*, void*) {}

// MDKInit stub (vtable[10]): called by the machine during its MDKInit.
// The argument is typically a buffer size or mode parameter that the machine
// expects to read back from the MDK stub later. Store it in field3.
static void __fastcall StubMDKInit(void* thisPtr, void* /*edx*/, void* dataInput) {
	auto* stub = (MDKStubObject*)thisPtr;
	stub->initCalled = 1;
	// Some machines pass a numeric argument (e.g., buffer size) as the dataInput.
	// Store it so the machine can read it back if needed.
	stub->field3 = (int)(intptr_t)dataInput;

	char dbg[128];
	snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] MDK vtable[10] (MDKInit) called on %p arg=%p\n",
		thisPtr, dataInput);
	OutputDebugStringA(dbg);
}

// MDK Setup stub (vtable[8]): Called from CMDKMachineInterface::Init (FUN_10003d30)
// with arg = -1. The real Buzz MDK implementation calls the machine's MDKInit
// through the back-pointer. MDKInit is at machine vtable index 23 (offset 0x5C).
static void __fastcall StubMDKSetup(void* thisPtr, void* /*edx*/, int /*mode*/) {
	auto* stub = (MDKStubObject*)thisPtr;
	void* machine = stub->machinePtr;

	char dbg[256];
	snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] MDK vtable[8] (Setup) called, machine=%p\n", machine);
	OutputDebugStringA(dbg);

	if (!machine) return;

	// Call machine->vtable[23](nullptr) to invoke MDKInit.
	// All MDK machines share the same CMDKMachineInterface vtable layout,
	// so index 23 is consistent. But some machines' MDKInit may hang
	// (e.g., waiting for a message loop). We use a thread with timeout.
	typedef void (__thiscall *MDKInitFn)(void*, void*);
	void** mVtable = *(void***)machine;
	MDKInitFn mdkInit = (MDKInitFn)mVtable[23];

	snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] Calling machine->vtable[23] (MDKInit) at %p\n",
		(void*)mdkInit);
	OutputDebugStringA(dbg);

	// Run MDKInit on a separate thread with a 3-second timeout.
	// Some machines hang here (e.g., BTDSys Pulsar).
	struct MDKInitParams {
		MDKInitFn fn;
		void* machine;
		bool completed;
		bool crashed;
		DWORD exceptionCode;
	};
	MDKInitParams initParams = { mdkInit, machine, false, false, 0 };

	HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
		auto* params = (MDKInitParams*)p;
		__try {
			params->fn(params->machine, nullptr);
			params->completed = true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
			params->crashed = true;
			params->exceptionCode = GetExceptionCode();
		}
		return 0;
	}, &initParams, 0, nullptr);

	if (hThread) {
		DWORD waitResult = WaitForSingleObject(hThread, 3000);
		if (waitResult == WAIT_TIMEOUT) {
			OutputDebugStringA("[BuzzBridgeHost32] MDKInit TIMED OUT (3s) — machine may hang during Init\n");
			TerminateThread(hThread, 1);
		} else if (initParams.crashed) {
			snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] MDKInit CRASHED (exception 0x%08lX)\n",
				initParams.exceptionCode);
			OutputDebugStringA(dbg);
		} else if (initParams.completed) {
			OutputDebugStringA("[BuzzBridgeHost32] MDKInit returned OK\n");
		}
		CloseHandle(hThread);
	} else {
		// Fallback: call directly with SEH
		__try {
			mdkInit(machine, nullptr);
			OutputDebugStringA("[BuzzBridgeHost32] MDKInit returned OK (direct call)\n");
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
			snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] MDKInit CRASHED (exception 0x%08lX)\n",
				GetExceptionCode());
			OutputDebugStringA(dbg);
		}
	}
}

// MDK Work proxy (vtable[6]): Called from CMDKMachineInterface::Work wrapper.
// The wrapper changes 'this' from machine to MDK, then calls MDK->vtable[6].
// We redirect back to the machine's vtable[22] (the overlap-add Work engine).
// Signature: bool __thiscall Work(float* psamples, int numsamples, int mode)
static int __fastcall StubMDKWork(void* thisPtr, void* /*edx*/,
                                   float* psamples, int numsamples, unsigned int mode) {
	auto* stub = (MDKStubObject*)thisPtr;
	void* machine = stub->machinePtr;
	if (!machine) return 0;

	// Call machine->vtable[22] (overlap-add Work = FUN_100033DE)
	typedef int (__thiscall *WorkFn)(void*, float*, int, unsigned int);
	void** mVtable = *(void***)machine;
	WorkFn workFn = (WorkFn)mVtable[22];
	return workFn(machine, psamples, numsamples, mode);
}

// MDK WorkMonoToStereo proxy (vtable[7]): Similar redirection.
// Signature: bool __thiscall WorkMonoToStereo(float* pin, float* pout, int numsamples, int mode)
static int __fastcall StubMDKWorkStereo(void* thisPtr, void* /*edx*/,
                                         float* pin, float* pout, int numsamples, unsigned int mode) {
	auto* stub = (MDKStubObject*)thisPtr;
	void* machine = stub->machinePtr;
	if (!machine) return 0;

	// MDK machines don't typically use WorkMonoToStereo, but redirect anyway.
	// The machine's vtable[22] is the stereo Work. For mono-to-stereo, there might
	// be a different entry, but for now just call the same Work with pin.
	typedef int (__thiscall *WorkFn)(void*, float*, int, unsigned int);
	void** mVtable = *(void***)machine;
	WorkFn workFn = (WorkFn)mVtable[22];
	return workFn(machine, pin, numsamples, mode);
}

// The MDK vtable. 32 entries to cover any method the machine might call.
static void* g_mdkVtable[32] = { nullptr };
static bool g_vtableInit = false;

static void InitVtable() {
	if (g_vtableInit) return;
	for (int i = 0; i < 32; i++) {
		g_mdkVtable[i] = (void*)StubNoOp2;
	}
	g_mdkVtable[0]  = (void*)StubNoOp1;      // destructor
	g_mdkVtable[6]  = (void*)StubMDKWork;     // Work proxy -> machine vtable[22]
	g_mdkVtable[7]  = (void*)StubMDKWorkStereo; // WorkMonoToStereo proxy
	g_mdkVtable[8]  = (void*)StubMDKSetup;    // Setup(int) - calls machine's MDKInit
	g_mdkVtable[10] = (void*)StubMDKInit;     // MDKInit tracking
	g_vtableInit = true;
}

void* CreateMDKStub() {
	InitVtable();

	auto* stub = (MDKStubObject*)calloc(1, sizeof(MDKStubObject));
	if (!stub) return nullptr;
	stub->vptr = g_mdkVtable;
	stub->initCalled = 0;

	// Subobject (0x28 bytes) with self-pointers, matching real Buzz
	void* sub = calloc(1, 0x28);
	if (sub) {
		*(void**)sub = sub;
		*((void**)sub + 1) = sub;
	}
	stub->subObject = sub;

	// Aligned buffer matching real Buzz: _aligned_malloc(0x840, 0x40)
	void* abuf = _aligned_malloc(0x840, 0x40);
	if (abuf) memset(abuf, 0, 0x840);
	stub->alignedBuf = abuf;

	char dbg[128];
	snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] Created MDK stub at %p vtable=%p\n",
		(void*)stub, (void*)g_mdkVtable);
	OutputDebugStringA(dbg);

	return stub;
}

void DestroyMDKStub(void* stub) {
	if (!stub) return;
	auto* s = (MDKStubObject*)stub;
	if (s->subObject) free(s->subObject);
	if (s->alignedBuf) _aligned_free(s->alignedBuf);
	free(s);
}

bool WasMDKInitCalled(void* stub) {
	if (!stub) return false;
	return ((MDKStubObject*)stub)->initCalled != 0;
}

void ResetMDKInitFlag(void* stub) {
	if (!stub) return;
	((MDKStubObject*)stub)->initCalled = 0;
}

} // namespace BuzzVst
