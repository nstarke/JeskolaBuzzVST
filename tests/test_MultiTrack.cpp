#include "TestFramework.h"
#include "TestHelpers.h"
#include "../src/buzz/BuzzParamLayout.h"
#include "../src/buzz/BuzzMachineLoader.h"
#include "../src/vst3/ParameterMapping.h"
#include "../src/vst3/plugids.h"
#include "../src/common/SEHGuard.h"

using namespace BuzzVst;

// ===========================================================================
// Parameter ID encoding/decoding
// ===========================================================================

TEST(MultiTrack, intEncodingTrack0) {
	// Track 0, param 0 -> ID 1000
	int id = kBuzzTrackParamBase + 0 * kTrackParamStride + 0;
	ASSERT_EQ((int)id, 1000);
}

TEST(MultiTrack, intEncodingTrack0Param5) {
	int id = kBuzzTrackParamBase + 0 * kTrackParamStride + 5;
	ASSERT_EQ((int)id, 1005);
}

TEST(MultiTrack, intEncodingTrack1) {
	int id = kBuzzTrackParamBase + 1 * kTrackParamStride + 0;
	ASSERT_EQ((int)id, 2000);
}

TEST(MultiTrack, intEncodingTrack15) {
	int id = kBuzzTrackParamBase + 15 * kTrackParamStride + 0;
	ASSERT_EQ((int)id, 16000);
}

TEST(MultiTrack, intDecodingTrack0) {
	int id = 1003;
	int trackOffset = id - kBuzzTrackParamBase;
	int trackIdx = trackOffset / kTrackParamStride;
	int slotIdx = trackOffset % kTrackParamStride;
	ASSERT_EQ(trackIdx, 0);
	ASSERT_EQ(slotIdx, 3);
}

TEST(MultiTrack, intDecodingTrack3Param7) {
	int id = kBuzzTrackParamBase + 3 * kTrackParamStride + 7;
	int trackOffset = id - kBuzzTrackParamBase;
	int trackIdx = trackOffset / kTrackParamStride;
	int slotIdx = trackOffset % kTrackParamStride;
	ASSERT_EQ(trackIdx, 3);
	ASSERT_EQ(slotIdx, 7);
}

TEST(MultiTrack, intRoundtrip) {
	for (int t = 0; t < kMaxTracks; t++) {
		for (int p = 0; p < 10; p++) {
			int id = kBuzzTrackParamBase + t * kTrackParamStride + p;
			int trackOffset = id - kBuzzTrackParamBase;
			int trackIdx = trackOffset / kTrackParamStride;
			int slotIdx = trackOffset % kTrackParamStride;
			CHECK_EQ(trackIdx, t);
			CHECK_EQ(slotIdx, p);
		}
	}
}

// ===========================================================================
// Multi-track parameter layout (BuzzParamLayout)
// ===========================================================================

TEST(MultiTrack, TrackParamLayoutMultipleTracks) {
	CMachineParameter gp = MakeParam(pt_byte, 0, 127, 255, 64);
	CMachineParameter tp1 = {pt_note, "Note", "Note", NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0};
	CMachineParameter tp2 = {pt_byte, "Volume", "Volume", 0, 0xFE, 0xFF, MPF_STATE, 0x80};

	const CMachineParameter* params[] = { &gp, &tp1, &tp2 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 2;
	info.Parameters = params;
	info.minTracks = 1;
	info.maxTracks = 8;

	BuzzParamLayout layout;
	layout.Build(&info);

	ASSERT_EQ(layout.GetTrackStructSize(), 2); // note(1) + byte(1)

	// Allocate buffer for 4 tracks
	int numTracks = 4;
	int trackSize = layout.GetTrackStructSize();
	std::vector<unsigned char> trackBuf(numTracks * trackSize, 0);

	// Write different notes to each track
	for (int t = 0; t < numTracks; t++) {
		int buzzNote = (t + 4) << 4 | 1; // C-4, C-5, C-6, C-7
		layout.WriteTrackParam(trackBuf.data(), t, 0, buzzNote);
		layout.WriteTrackParam(trackBuf.data(), t, 1, 100 + t);
	}

	// Read back and verify each track is independent
	for (int t = 0; t < numTracks; t++) {
		int expectedNote = (t + 4) << 4 | 1;
		ASSERT_EQ(layout.ReadTrackParam(trackBuf.data(), t, 0), expectedNote);
		ASSERT_EQ(layout.ReadTrackParam(trackBuf.data(), t, 1), 100 + t);
	}
}

TEST(MultiTrack, TrackParamNoValuesPerTrack) {
	CMachineParameter tp1 = {pt_note, "Note", "Note", NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0};
	CMachineParameter tp2 = {pt_byte, "Volume", "Volume", 0, 0xFE, 0xFF, MPF_STATE, 0x80};

	const CMachineParameter* params[] = { &tp1, &tp2 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 0;
	info.numTrackParameters = 2;
	info.Parameters = params;
	info.minTracks = 1;
	info.maxTracks = 8;

	BuzzParamLayout layout;
	layout.Build(&info);

	int numTracks = 3;
	int trackSize = layout.GetTrackStructSize();
	std::vector<unsigned char> trackBuf(numTracks * trackSize, 0);

	// Write values to track 1
	layout.WriteTrackParam(trackBuf.data(), 1, 0, 0x51); // C-5
	layout.WriteTrackParam(trackBuf.data(), 1, 1, 120);

	// Set all to NoValue
	layout.WriteTrackAllNoValues(trackBuf.data(), numTracks);

	// All tracks should have NoValues
	for (int t = 0; t < numTracks; t++) {
		ASSERT_EQ(layout.ReadTrackParam(trackBuf.data(), t, 0), NOTE_NO);
		ASSERT_EQ(layout.ReadTrackParam(trackBuf.data(), t, 1), 0xFF);
	}
}

TEST(MultiTrack, TracksAreNonOverlapping) {
	// Verify that writing to track N doesn't corrupt track N-1 or N+1
	CMachineParameter tp = MakeParam(pt_word, 0, 65535, 65535, 1000);

	const CMachineParameter* params[] = { &tp };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 0;
	info.numTrackParameters = 1;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	int numTracks = 8;
	std::vector<unsigned char> trackBuf(numTracks * layout.GetTrackStructSize(), 0);

	// Write unique values to each track
	for (int t = 0; t < numTracks; t++) {
		layout.WriteTrackParam(trackBuf.data(), t, 0, 1000 + t * 111);
	}

	// Verify each track has its own value
	for (int t = 0; t < numTracks; t++) {
		ASSERT_EQ(layout.ReadTrackParam(trackBuf.data(), t, 0), 1000 + t * 111);
	}
}

// ===========================================================================
// Constants and limits
// ===========================================================================

TEST(MultiTrack, MaxTracksConstant) {
	ASSERT_EQ(kMaxTracks, 16);
}

TEST(MultiTrack, TrackParamStrideConstant) {
	ASSERT_EQ(kTrackParamStride, 1000);
}

TEST(MultiTrack, MaxTrackParamsConstant) {
	ASSERT_EQ(kMaxTrackParams, 64);
}

TEST(MultiTrack, BypassintAboveAllTracks) {
	// Bypass ID should be above all track param IDs
	int maxTrackParamId = kBuzzTrackParamBase + (kMaxTracks - 1) * kTrackParamStride + kMaxTrackParams;
	ASSERT_GT((int)kBypassParamID, maxTrackParamId);
}

TEST(MultiTrack, NoTrackintOverlapsGlobal) {
	// Track 0 params start at 1000, globals end at 128
	ASSERT_GT((int)kBuzzTrackParamBase, kBuzzGlobalParamBase + kMaxGlobalParams);
}

// ===========================================================================
// Integration: real machine with tracks
// ===========================================================================

// GetGearPath is in TestHelpers.h

TEST(MultiTrack, RealMachineTrackInfo) {
	// Load a generator and check its track limits
	std::string path = GetGearPath("generators\\FSM Kick XP.dll");

	BuzzMachineLoader loader;
	if (!loader.Load(path.c_str())) {
		printf("  (skipped - DLL not found) ");
		return;
	}

	auto* info = loader.GetInfo();
	ASSERT_NOT_NULL(info);

	// FSM Kick XP should have track params (Note + maybe Volume)
	// Just verify the track counts are sane
	ASSERT_GE(info->minTracks, 0);
	ASSERT_GE(info->maxTracks, info->minTracks);
	ASSERT_LE(info->maxTracks, 256);

	printf("  (minTracks=%d, maxTracks=%d, trackParams=%d) ",
		info->minTracks, info->maxTracks, info->numTrackParameters);
}

TEST(MultiTrack, RealMachineSetNumTracks) {
	std::string path = GetGearPath("generators\\FSM Kick XP.dll");

	BuzzMachineLoader loader;
	if (!loader.Load(path.c_str())) {
		printf("  (skipped) ");
		return;
	}

	auto* info = loader.GetInfo();
	if (!info || info->maxTracks <= info->minTracks) {
		printf("  (skipped - machine doesn't support multiple tracks) ");
		return;
	}

	loader.UpdateMasterInfo(125.0, 44100.0);
	if (!loader.InitMachine()) {
		printf("  (init failed) ");
		return;
	}

	auto* machine = loader.GetMachine();

	// Try setting to maxTracks
	int target = std::min(info->maxTracks, 4);
	bool ok = SEH_Call([&]() {
		machine->SetNumTracks(target);
	});
	ASSERT_TRUE(ok);

	// Tick with the new track count should not crash
	auto* layout = loader.GetParamLayout();
	if (machine->GlobalVals)
		layout->WriteAllNoValues(machine->GlobalVals);

	if (machine->TrackVals && info->numTrackParameters > 0)
		layout->WriteTrackAllNoValues(machine->TrackVals, target);

	ok = SEH_Call([&]() { machine->Tick(); });
	ASSERT_TRUE(ok);
}

// ===========================================================================
// 2D track value array management
// ===========================================================================

TEST(MultiTrack, TrackArrayInitialization) {
	// Simulate what loadBuzzMachine does: init 2D arrays
	int numTracks = 3;
	int numParams = 2;

	std::vector<std::vector<int>> trackValues(numTracks);
	std::vector<std::vector<bool>> trackChanged(numTracks);

	for (int t = 0; t < numTracks; t++) {
		trackValues[t].resize(numParams, 0);
		trackChanged[t].resize(numParams, false);
	}

	ASSERT_EQ((int)trackValues.size(), 3);
	ASSERT_EQ((int)trackValues[0].size(), 2);
	ASSERT_EQ((int)trackValues[2].size(), 2);
}

TEST(MultiTrack, TrackArrayResize) {
	// Simulate adding a track
	int numParams = 2;
	std::vector<std::vector<int>> trackValues(1);
	trackValues[0] = {100, 200};

	// Add a second track
	trackValues.resize(2);
	trackValues[1].resize(numParams, 0);

	ASSERT_EQ((int)trackValues.size(), 2);
	// Track 0 should be unchanged
	ASSERT_EQ(trackValues[0][0], 100);
	ASSERT_EQ(trackValues[0][1], 200);
	// Track 1 should be at defaults
	ASSERT_EQ(trackValues[1][0], 0);
	ASSERT_EQ(trackValues[1][1], 0);
}

TEST(MultiTrack, TrackArrayShrink) {
	std::vector<std::vector<int>> trackValues(4);
	for (int t = 0; t < 4; t++) {
		trackValues[t] = {t * 10, t * 10 + 1};
	}

	// Shrink to 2 tracks
	trackValues.resize(2);

	ASSERT_EQ((int)trackValues.size(), 2);
	ASSERT_EQ(trackValues[0][0], 0);
	ASSERT_EQ(trackValues[1][0], 10);
}

// ===========================================================================
// Parameter mapping with track-aware IDs
// ===========================================================================

TEST(MultiTrack, ParameterMappingPerTrack) {
	CMachineParameter pVol = {pt_byte, "Volume", "Volume", 0, 0xFE, 0xFF, MPF_STATE, 0x80};

	// Track 0 volume at 50% -> VST3 normalized
	double norm = ParameterMapping::BuzzToNormalized(0x7F, &pVol);
	int back = ParameterMapping::NormalizedToBuzz(norm, &pVol);
	ASSERT_EQ(back, 0x7F);

	// Same mapping applies to all tracks (parameter definition is shared)
	for (int t = 0; t < 4; t++) {
		double n = ParameterMapping::BuzzToNormalized(100 + t, &pVol);
		int b = ParameterMapping::NormalizedToBuzz(n, &pVol);
		CHECK_EQ(b, 100 + t);
	}
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST(MultiTrack, ZeroTracksHandledSafely) {
	// Machine with 0 minTracks, 0 maxTracks -> no track params
	CMachineParameter gp = MakeParam(pt_byte, 0, 127, 255, 64);
	const CMachineParameter* params[] = { &gp };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 0;
	info.Parameters = params;
	info.minTracks = 0;
	info.maxTracks = 0;

	BuzzParamLayout layout;
	layout.Build(&info);

	ASSERT_EQ(layout.GetTrackStructSize(), 0);
	ASSERT_EQ((int)layout.GetTrackSlots().size(), 0);
}

TEST(MultiTrack, SingleTrackMachine) {
	CMachineParameter tp = {pt_note, "Note", "Note", NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0};
	const CMachineParameter* params[] = { &tp };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 0;
	info.numTrackParameters = 1;
	info.Parameters = params;
	info.minTracks = 1;
	info.maxTracks = 1;

	BuzzParamLayout layout;
	layout.Build(&info);

	// Single track is still valid
	unsigned char trackBuf[4] = {};
	layout.WriteTrackParam(trackBuf, 0, 0, 0x51);
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 0), 0x51);
}
