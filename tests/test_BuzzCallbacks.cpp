#include <windows.h>
#include "TestFramework.h"
#include "../src/buzz/MachineInterface.h"
#include "../src/buzz/BuzzCallbacks.h"

using namespace BuzzVst;

// ===== Construction =====

TEST(Callbacks, ConstructionDoesNotCrash) {
	BuzzCallbacks cb;
	ASSERT_TRUE(true);
}

// ===== Lock/Unlock =====

TEST(Callbacks, LockUnlockDoesNotDeadlock) {
	BuzzCallbacks cb;
	cb.Lock();
	cb.Unlock();
	ASSERT_TRUE(true);
}

TEST(Callbacks, DoubleLockDoesNotDeadlock) {
	// CriticalSection is reentrant on Windows
	BuzzCallbacks cb;
	cb.Lock();
	cb.Lock();
	cb.Unlock();
	cb.Unlock();
	ASSERT_TRUE(true);
}

// ===== Oscillator Tables =====

TEST(Callbacks, GetOscillatorTableSine) {
	BuzzCallbacks cb;
	const short* table = cb.GetOscillatorTable(OWF_SINE);
	ASSERT_NOT_NULL(table);
}

TEST(Callbacks, GetOscillatorTableAllWaveforms) {
	BuzzCallbacks cb;
	for (int i = 0; i < 6; i++) {
		const short* table = cb.GetOscillatorTable(i);
		ASSERT_NOT_NULL(table);
	}
}

TEST(Callbacks, GetOscillatorTableInvalid) {
	BuzzCallbacks cb;
	ASSERT_NULL(cb.GetOscillatorTable(-1));
	ASSERT_NULL(cb.GetOscillatorTable(100));
}

// ===== Host Version Detection =====

TEST(Callbacks, GetNearestWaveLevelVersionCheck) {
	BuzzCallbacks cb;
	// GetNearestWaveLevel(-2, -2) should return non-null to indicate version support
	const CWaveLevel* result = cb.GetNearestWaveLevel(-2, -2);
	ASSERT_NOT_NULL(result);
}

TEST(Callbacks, GetNearestWaveLevelNormalReturnsNull) {
	BuzzCallbacks cb;
	// Normal wave access should return null (we don't host waves)
	const CWaveLevel* result = cb.GetNearestWaveLevel(1, 60);
	ASSERT_NULL(result);
}

// ===== Host Version =====

TEST(Callbacks, GetHostVersion) {
	BuzzCallbacks cb;
	int ver = cb.GetHostVersion();
	ASSERT_EQ(ver, MI_VERSION);
}

// ===== State Flags =====

TEST(Callbacks, GetStateFlags) {
	BuzzCallbacks cb;
	int flags = cb.GetStateFlags();
	ASSERT_TRUE((flags & SF_PLAYING) != 0);
}

// ===== Aux Buffer =====

TEST(Callbacks, GetAuxBufferNotNull) {
	BuzzCallbacks cb;
	float* buf = cb.GetAuxBuffer();
	ASSERT_NOT_NULL(buf);
}

TEST(Callbacks, ClearAuxBuffer) {
	BuzzCallbacks cb;
	float* buf = cb.GetAuxBuffer();
	buf[0] = 1.0f;
	buf[1] = 2.0f;
	cb.ClearAuxBuffer();
	ASSERT_NEAR(buf[0], 0.0f, 0.0001f);
	ASSERT_NEAR(buf[1], 0.0f, 0.0001f);
}

// ===== Wave access (should return safe defaults) =====

TEST(Callbacks, GetWaveReturnsNull) {
	BuzzCallbacks cb;
	ASSERT_NULL(cb.GetWave(0));
	ASSERT_NULL(cb.GetWave(1));
}

TEST(Callbacks, GetWaveLevelReturnsNull) {
	BuzzCallbacks cb;
	ASSERT_NULL(cb.GetWaveLevel(0, 0));
}

// ===== Safe default returns =====

TEST(Callbacks, GetWaveNameReturnsEmpty) {
	BuzzCallbacks cb;
	const char* name = cb.GetWaveName(0);
	ASSERT_NOT_NULL(name);
	ASSERT_EQ(strlen(name), (size_t)0);
}

TEST(Callbacks, GetMachineNameReturnsNonNull) {
	BuzzCallbacks cb;
	const char* name = cb.GetMachineName(nullptr);
	ASSERT_NOT_NULL(name);
}

TEST(Callbacks, GetThisMachineReturnsNonNull) {
	BuzzCallbacks cb;
	ASSERT_NOT_NULL(cb.GetThisMachine());
}

TEST(Callbacks, GetBuildNumber) {
	BuzzCallbacks cb;
	int build = cb.GetBuildNumber();
	ASSERT_GT(build, 0);
}

TEST(Callbacks, GetBaseOctave) {
	BuzzCallbacks cb;
	int octave = cb.GetBaseOctave();
	ASSERT_GE(octave, 0);
	ASSERT_LE(octave, 9);
}

TEST(Callbacks, IsSongClosingReturnsFalse) {
	BuzzCallbacks cb;
	ASSERT_FALSE(cb.IsSongClosing());
}

// ===== Master Info Pointer =====

TEST(Callbacks, MasterInfoPointer) {
	BuzzCallbacks cb;
	CMasterInfo mi = {};
	mi.BeatsPerMin = 140;
	mi.SamplesPerSec = 48000;
	cb.masterInfoPtr = &mi;

	ASSERT_EQ(cb.GetTempo(), 140);
}

TEST(Callbacks, MasterInfoNullFallback) {
	BuzzCallbacks cb;
	cb.masterInfoPtr = nullptr;
	// Should return safe default (125 BPM)
	ASSERT_EQ(cb.GetTempo(), 125);
}

// ===== Profile functions (safe no-ops) =====

TEST(Callbacks, GetProfileIntReturnsDefault) {
	BuzzCallbacks cb;
	ASSERT_EQ(cb.GetProfileInt("test", 42), 42);
}

TEST(Callbacks, GetProfileBinaryReturnsNull) {
	BuzzCallbacks cb;
	byte* data = nullptr;
	int nbytes = 0;
	cb.GetProfileBinary("test", &data, &nbytes);
	ASSERT_NULL(data);
	ASSERT_EQ(nbytes, 0);
}

// ===== Machine Info =====

TEST(Callbacks, MachineInfoRoundtrip) {
	BuzzCallbacks cb;
	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.Name = "Test Machine";
	cb.machineInfo = &info;

	const CMachineInfo* retrieved = cb.GetMachineInfo(nullptr);
	ASSERT_NOT_NULL(retrieved);
	ASSERT_EQ(retrieved->Type, MT_GENERATOR);
	ASSERT_EQ(strcmp(retrieved->Name, "Test Machine"), 0);
}

// ===== Vtable order sanity check =====
// This test verifies that calling methods through a CMICallbacks* pointer
// dispatches correctly (critical for ABI compatibility with Buzz DLLs)

TEST(Callbacks, VtableDispatchThroughBasePointer) {
	BuzzCallbacks cb;
	CMICallbacks* base = &cb;

	// These should call BuzzCallbacks overrides, not the stubs
	const short* table = base->GetOscillatorTable(OWF_SINE);
	ASSERT_NOT_NULL(table);

	int flags = base->GetStateFlags();
	ASSERT_TRUE((flags & SF_PLAYING) != 0);

	float* aux = base->GetAuxBuffer();
	ASSERT_NOT_NULL(aux);
}

// ===== MessageBox doesn't crash =====

TEST(Callbacks, MessageBoxDoesNotCrash) {
	BuzzCallbacks cb;
	cb.MessageBox("test message");
	cb.MessageBox(nullptr);
	ASSERT_TRUE(true);
}

// ===== DebugLock delegates to Lock =====

TEST(Callbacks, DebugLockDoesNotCrash) {
	BuzzCallbacks cb;
	cb.DebugLock("test_location");
	cb.Unlock(); // must unlock what DebugLock locked
	ASSERT_TRUE(true);
}
