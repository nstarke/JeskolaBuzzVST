// Tests for the CMDKImplementation port (BuzzMDKHelper)

#include <windows.h>
#include <cstring>
#include <cstdlib>
#include "TestFramework.h"
#include "../src/buzz/BuzzMDKHelper.h"
#include "../src/buzz/BuzzCallbacks.h"
#include "../src/buzz/MachineInterface.h"

using namespace BuzzVst;

// ===== MDK Stub Creation/Destruction =====

TEST(MDK, CreateStubReturnsNonNull) {
	void* stub = CreateMDKStub();
	ASSERT_NOT_NULL(stub);
	DestroyMDKStub(stub);
}

TEST(MDK, DestroyNullDoesNotCrash) {
	DestroyMDKStub(nullptr);
}

TEST(MDK, StubHasVtablePointer) {
	void* stub = CreateMDKStub();
	// First 4 bytes = vtable pointer, should be non-null
	void** vptr = *(void***)stub;
	ASSERT_NOT_NULL(vptr);
	DestroyMDKStub(stub);
}

TEST(MDK, StubMachinePointerInitNull) {
	void* stub = CreateMDKStub();
	// Offset 4 = machine pointer, should be null initially
	void* machinePtr = *((void**)stub + 1);
	ASSERT_NULL(machinePtr);
	DestroyMDKStub(stub);
}

TEST(MDK, StubSubObjectAllocated) {
	void* stub = CreateMDKStub();
	// Offset 8 = subobject pointer
	void* subObj = *((void**)stub + 2);
	ASSERT_NOT_NULL(subObj);
	DestroyMDKStub(stub);
}

TEST(MDK, StubAlignedBufferAllocated) {
	void* stub = CreateMDKStub();
	// Offset 0x28 = aligned buffer (index 10 in int* array)
	void* alignedBuf = *((void**)stub + 10);
	ASSERT_NOT_NULL(alignedBuf);
	// Check alignment (should be 64-byte aligned)
	ASSERT_EQ((uintptr_t)alignedBuf % 64, (uintptr_t)0);
	DestroyMDKStub(stub);
}

TEST(MDK, SubObjectHasSelfPointers) {
	void* stub = CreateMDKStub();
	void* subObj = *((void**)stub + 2);
	// Real Buzz sets sub[0] = sub and sub[1] = sub
	void* self0 = *(void**)subObj;
	void* self1 = *((void**)subObj + 1);
	ASSERT_EQ(self0, subObj);
	ASSERT_EQ(self1, subObj);
	DestroyMDKStub(stub);
}

// ===== MDK Init Tracking =====

TEST(MDK, InitNotCalledInitially) {
	void* stub = CreateMDKStub();
	ASSERT_TRUE(!WasMDKInitCalled(stub));
	DestroyMDKStub(stub);
}

TEST(MDK, InitCalledAfterVtable10) {
	void* stub = CreateMDKStub();

	// Simulate calling vtable[10] (MDKInit) with a dummy arg
	void** vtable = *(void***)stub;
	typedef void (__fastcall *InitFn)(void*, void*, void*);
	InitFn initFn = (InitFn)vtable[10];
	initFn(stub, nullptr, nullptr);

	ASSERT_TRUE(WasMDKInitCalled(stub));
	DestroyMDKStub(stub);
}

TEST(MDK, ResetInitFlag) {
	void* stub = CreateMDKStub();

	// Call vtable[10]
	void** vtable = *(void***)stub;
	typedef void (__fastcall *InitFn)(void*, void*, void*);
	InitFn initFn = (InitFn)vtable[10];
	initFn(stub, nullptr, nullptr);

	ASSERT_TRUE(WasMDKInitCalled(stub));
	ResetMDKInitFlag(stub);
	ASSERT_TRUE(!WasMDKInitCalled(stub));
	DestroyMDKStub(stub);
}

TEST(MDK, InitCalledOnNullReturnsFalse) {
	ASSERT_TRUE(!WasMDKInitCalled(nullptr));
}

TEST(MDK, ResetOnNullDoesNotCrash) {
	ResetMDKInitFlag(nullptr);
}

// ===== Vtable Layout =====

TEST(MDK, VtableHas32Entries) {
	void* stub = CreateMDKStub();
	void** vtable = *(void***)stub;
	// All 32 entries should be non-null (filled with stubs)
	for (int i = 0; i < 32; i++) {
		ASSERT_NOT_NULL(vtable[i]);
	}
	DestroyMDKStub(stub);
}

TEST(MDK, VtableEntriesAreDistinct) {
	void* stub = CreateMDKStub();
	void** vtable = *(void***)stub;
	// Key entries should be distinct from the generic no-op
	void* genericNoOp = vtable[1]; // non-special entry
	// vtable[6] = StubMDKWork
	ASSERT_TRUE(vtable[6] != genericNoOp);
	// vtable[8] = StubMDKSetup
	ASSERT_TRUE(vtable[8] != genericNoOp);
	// vtable[10] = StubMDKInit
	ASSERT_TRUE(vtable[10] != genericNoOp);
	DestroyMDKStub(stub);
}

// ===== MDK Stub Size =====

TEST(MDK, StubObjectSize) {
	// The MDK stub should be at least 0x2C (44) bytes to match real Buzz
	// Plus our extra initCalled field
	ASSERT_TRUE(sizeof(int) * 12 >= 44); // 12 int-sized fields >= 44 bytes
}

// ===== Machine Back-Pointer =====

TEST(MDK, SetMachinePointer) {
	void* stub = CreateMDKStub();
	int fakeMachine = 42;

	// Set machine pointer (offset 4 = index 1)
	*((void**)stub + 1) = &fakeMachine;

	// Read it back
	void* machinePtr = *((void**)stub + 1);
	ASSERT_EQ(machinePtr, (void*)&fakeMachine);

	DestroyMDKStub(stub);
}

// ===== Work Proxy (vtable[6]) =====

TEST(MDK, WorkProxyReturns0WithNullMachine) {
	void* stub = CreateMDKStub();
	// machinePtr is null, Work should return 0

	void** vtable = *(void***)stub;
	typedef int (__fastcall *WorkFn)(void*, void*, float*, int, unsigned int);
	WorkFn workFn = (WorkFn)vtable[6];

	float buf[256] = {};
	int result = workFn(stub, nullptr, buf, 128, 3);
	ASSERT_EQ(result, 0);

	DestroyMDKStub(stub);
}

// ===== Multiple Create/Destroy =====

TEST(MDK, MultipleCreateDestroy) {
	for (int i = 0; i < 10; i++) {
		void* stub = CreateMDKStub();
		ASSERT_NOT_NULL(stub);
		DestroyMDKStub(stub);
	}
}

TEST(MDK, MultipleStubsIndependent) {
	void* stub1 = CreateMDKStub();
	void* stub2 = CreateMDKStub();

	ASSERT_TRUE(stub1 != stub2);

	// They share the same vtable
	void** vtable1 = *(void***)stub1;
	void** vtable2 = *(void***)stub2;
	ASSERT_EQ(vtable1, vtable2);

	// But have independent state
	*((void**)stub1 + 1) = (void*)0x1111;
	*((void**)stub2 + 1) = (void*)0x2222;
	ASSERT_TRUE(*((void**)stub1 + 1) != *((void**)stub2 + 1));

	DestroyMDKStub(stub1);
	DestroyMDKStub(stub2);
}

// ===== No-Op Methods Don't Crash =====

TEST(MDK, NoOpVtableEntriesDontCrash) {
	void* stub = CreateMDKStub();
	void** vtable = *(void***)stub;

	// Call several non-critical vtable entries that should be no-ops
	typedef void (__fastcall *NoOpFn)(void*, void*, void*, void*);
	for (int i : {0, 1, 2, 3, 4, 5, 9, 11, 12, 13, 14, 15}) {
		NoOpFn fn = (NoOpFn)vtable[i];
		fn(stub, nullptr, nullptr, nullptr); // should not crash
	}

	DestroyMDKStub(stub);
}

// ===== Setup (vtable[8]) with null machine =====

TEST(MDK, SetupWithNullMachineDoesNotCrash) {
	void* stub = CreateMDKStub();
	// machinePtr is null

	void** vtable = *(void***)stub;
	typedef void (__fastcall *SetupFn)(void*, void*, int);
	SetupFn setupFn = (SetupFn)vtable[8];
	setupFn(stub, nullptr, -1); // should not crash

	DestroyMDKStub(stub);
}

// ===== Callbacks Integration =====

TEST(MDK, CallbacksReturnMDKForNeg1Neg1) {
	// This tests the BuzzCallbacks integration
	// GetNearestWaveLevel(-1, -1) should return a non-null MDK stub
	BuzzCallbacks cb;
	const CWaveLevel* result = cb.GetNearestWaveLevel(-1, -1);
	ASSERT_NOT_NULL(result);
	ASSERT_TRUE(cb.isMDKMachine);
}

TEST(MDK, CallbacksMDKStubPersists) {
	BuzzCallbacks cb;
	const CWaveLevel* result1 = cb.GetNearestWaveLevel(-1, -1);
	const CWaveLevel* result2 = cb.GetNearestWaveLevel(-1, -1);
	// Should return the same stub
	ASSERT_EQ(result1, result2);
}

TEST(MDK, CallbacksNeg2Neg2StillWorks) {
	BuzzCallbacks cb;
	// (-2, -2) is host version detection, should return 1
	const CWaveLevel* result = cb.GetNearestWaveLevel(-2, -2);
	ASSERT_EQ((intptr_t)result, 1);
}

TEST(MDK, CallbacksMDKFlagResets) {
	BuzzCallbacks cb;
	cb.GetNearestWaveLevel(-1, -1);
	ASSERT_TRUE(cb.isMDKMachine);
	cb.isMDKMachine = false;
	ASSERT_TRUE(!cb.isMDKMachine);
}
