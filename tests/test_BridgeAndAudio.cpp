// Tests for BridgeIPC helpers, BuzzSampleToVst3 conversion, DescribeValue protocol,
// wave table boundary conditions, and parameter persistence data flow.

#include "TestFramework.h"
#include "../src/bridge/BridgeIPC.h"
#include "../src/vst3/plugids.h"
#include "../src/buzz/BuzzWaveTable.h"
#include "../src/buzz/BuzzParamLayout.h"
#include "../src/vst3/ParameterMapping.h"
#include "../src/buzz/MachineInterface.h"
#include <cstring>
#include <cmath>
#include <limits>

using namespace BuzzVst;

// ============================================================================
// 1. New BridgeIPC structs (kCmdDescribeValue, kRespDescribeValue)
// ============================================================================

TEST(BridgeIPCNew, DescribeValueStructSize) {
    ASSERT_EQ(sizeof(BridgeDescribeValue), 8u);
}

TEST(BridgeIPCNew, DescribeValueLayout) {
    BridgeDescribeValue dv = {};
    dv.param = 42;
    dv.value = 100;
    ASSERT_EQ(dv.param, 42);
    ASSERT_EQ(dv.value, 100);
}

TEST(BridgeIPCNew, DescribeValueCommandExists) {
    // kCmdDescribeValue should be between kCmdSetNumTracks and kCmdShutdown
    ASSERT_GT((uint32_t)kCmdDescribeValue, (uint32_t)kCmdSetNumTracks);
    ASSERT_LT((uint32_t)kCmdDescribeValue, (uint32_t)kCmdShutdown);
}

TEST(BridgeIPCNew, DescribeValueResponseExists) {
    ASSERT_GT((uint32_t)kRespDescribeValue, (uint32_t)kRespMachineInfo);
}

// ============================================================================
// 3. BuzzSampleToVst3 conversion (plugids.h)
// ============================================================================

TEST(AudioConversion, BuzzToVst3_Zero) {
    ASSERT_NEAR(BuzzSampleToVst3(0.0f), 0.0f, 0.0001f);
}

TEST(AudioConversion, BuzzToVst3_FullScale) {
    ASSERT_NEAR(BuzzSampleToVst3(32768.0f), 1.0f, 0.0001f);
    ASSERT_NEAR(BuzzSampleToVst3(-32768.0f), -1.0f, 0.0001f);
}

TEST(AudioConversion, BuzzToVst3_HalfScale) {
    ASSERT_NEAR(BuzzSampleToVst3(16384.0f), 0.5f, 0.001f);
    ASSERT_NEAR(BuzzSampleToVst3(-16384.0f), -0.5f, 0.001f);
}

TEST(AudioConversion, BuzzToVst3_Clamp) {
    // Values beyond ±32768 should clamp to ±1.0
    ASSERT_EQ(BuzzSampleToVst3(100000.0f), 1.0f);
    ASSERT_EQ(BuzzSampleToVst3(-100000.0f), -1.0f);
}

TEST(AudioConversion, BuzzToVst3_NaN) {
    float nan = std::numeric_limits<float>::quiet_NaN();
    ASSERT_EQ(BuzzSampleToVst3(nan), 0.0f);
}

TEST(AudioConversion, BuzzToVst3_Infinity) {
    float inf = std::numeric_limits<float>::infinity();
    ASSERT_EQ(BuzzSampleToVst3(inf), 1.0f);
    ASSERT_EQ(BuzzSampleToVst3(-inf), -1.0f);
}

TEST(AudioConversion, BuzzToVst3_SmallValues) {
    // Small values should pass through linearly
    float val = 100.0f;
    float expected = 100.0f / 32768.0f;
    ASSERT_NEAR(BuzzSampleToVst3(val), expected, 0.00001f);
}

TEST(AudioConversion, ScaleConstants) {
    ASSERT_NEAR(kBuzzToVst3Scale, 1.0f / 32768.0f, 0.000001f);
    ASSERT_NEAR(kVst3ToBuzzScale, 32768.0f, 0.001f);
    ASSERT_NEAR(kBuzzToVst3Scale * kVst3ToBuzzScale, 1.0f, 0.0001f);
}

// ============================================================================
// 4. Wave table boundary tests
// ============================================================================

TEST(WaveTableBounds, SlotMin) {
    BuzzWaveTable wt;
    // Slot 1 (WAVE_MIN) should be valid
    ASSERT_EQ(wt.IsLoaded(1), false);
    ASSERT_EQ(std::string(wt.GetWaveName(1)), std::string(""));
}

TEST(WaveTableBounds, SlotMax) {
    BuzzWaveTable wt;
    // Slot 200 (WAVE_MAX) should be valid
    ASSERT_EQ(wt.IsLoaded(200), false);
    ASSERT_EQ(std::string(wt.GetWaveName(200)), std::string(""));
}

TEST(WaveTableBounds, SlotZeroInvalid) {
    BuzzWaveTable wt;
    ASSERT_EQ(wt.IsLoaded(0), false);
    ASSERT_EQ(wt.GetWave(0), nullptr);
    ASSERT_EQ(wt.GetWaveLevel(0, 0), nullptr);
}

TEST(WaveTableBounds, SlotNegativeInvalid) {
    BuzzWaveTable wt;
    ASSERT_EQ(wt.IsLoaded(-1), false);
    ASSERT_EQ(wt.GetWave(-1), nullptr);
}

TEST(WaveTableBounds, Slot201Invalid) {
    BuzzWaveTable wt;
    ASSERT_EQ(wt.IsLoaded(201), false);
    ASSERT_EQ(wt.GetWave(201), nullptr);
}

TEST(WaveTableBounds, GetFreeWaveEmpty) {
    BuzzWaveTable wt;
    // First free slot should be 1
    ASSERT_EQ(wt.GetFreeWave(), 1);
}

TEST(WaveTableBounds, ClearInvalidSlot) {
    BuzzWaveTable wt;
    // Should not crash
    wt.Clear(0);
    wt.Clear(-1);
    wt.Clear(201);
    ASSERT_EQ(wt.GetFreeWave(), 1);
}

TEST(WaveTableBounds, GetNearestWaveLevelInvalid) {
    BuzzWaveTable wt;
    ASSERT_EQ(wt.GetNearestWaveLevel(0, 60), nullptr);
    ASSERT_EQ(wt.GetNearestWaveLevel(-1, 60), nullptr);
    ASSERT_EQ(wt.GetNearestWaveLevel(201, 60), nullptr);
}

// ============================================================================
// 5. Parameter ID encoding/decoding (supplements test_Comprehensive ParamIDs)
// ============================================================================

TEST(ParamIDEncoding, TrackParamRoundtrip) {
    // Encode and decode track params for various track/param combos
    int numTrackParams = 9;
    for (int t = 0; t < 4; t++) {
        for (int p = 0; p < numTrackParams; p++) {
            int encoded = 1000 + t * numTrackParams + p;
            int decodedTrack = (encoded - 1000) / numTrackParams;
            int decodedParam = (encoded - 1000) % numTrackParams;
            ASSERT_EQ(decodedTrack, t);
            ASSERT_EQ(decodedParam, p);
        }
    }
}

// ============================================================================
// 6. MIDI CC mapping range validation
// ============================================================================

TEST(MidiCCMapping, GlobalCCRange) {
    // CCs 0-63 should map to global params
    for (int cc = 0; cc < (int)kMaxGlobalParams; cc++) {
        int id = kBuzzGlobalParamBase + cc;
        ASSERT_GE(id, (int)kBuzzGlobalParamBase);
        ASSERT_LT(id, (int)kBuzzGlobalParamBase + (int)kMaxGlobalParams);
    }
}

TEST(MidiCCMapping, TrackCCRange) {
    // CCs after globals should map to track 0 params
    for (int cc = 0; cc < (int)kMaxTrackParams; cc++) {
        int id = kBuzzTrackParamBase + cc;
        ASSERT_GE(id, (int)kBuzzTrackParamBase);
        ASSERT_LT(id, (int)kBuzzTrackParamBase + (int)kMaxTrackParams);
    }
}

TEST(MidiCCMapping, GlobalTrackNoOverlap) {
    int lastGlobalCC = kBuzzGlobalParamBase + kMaxGlobalParams - 1;
    ASSERT_LT(lastGlobalCC, (int)kBuzzTrackParamBase);
}

// ============================================================================
// 7. ParameterMapping with non-zero min (deferred restore path)
// ============================================================================

static CMachineParameter MakeParam(int type, int mn, int mx, int noVal, int defVal, int flags = MPF_STATE) {
    CMachineParameter p = {};
    p.Type = (CMPType)type;
    p.MinValue = mn;
    p.MaxValue = mx;
    p.NoValue = noVal;
    p.DefValue = defVal;
    p.Flags = flags;
    p.Name = "Test";
    return p;
}

TEST(DeferredParamRestore, NonZeroMinConversion) {
    // Simulates the deferred restore path: (buzzValue - mn) / stepCount
    CMachineParameter p = MakeParam(pt_byte, 92, 164, 255, 128);
    int stepCount = p.MaxValue - p.MinValue; // 72
    int buzzValue = 128;
    double normalized = (double)(buzzValue - p.MinValue) / (double)stepCount;
    ASSERT_NEAR(normalized, 0.5, 0.01);

    // Verify matches ParameterMapping
    ASSERT_NEAR(ParameterMapping::BuzzToNormalized(buzzValue, &p), normalized, 0.001);
}

TEST(DeferredParamRestore, MinValueConversion) {
    CMachineParameter p = MakeParam(pt_byte, 10, 110, 255, 60);
    int stepCount = 100;
    double norm = (double)(10 - 10) / (double)stepCount;
    ASSERT_NEAR(norm, 0.0, 0.001);
    ASSERT_NEAR(ParameterMapping::BuzzToNormalized(10, &p), 0.0, 0.001);
}

TEST(DeferredParamRestore, MaxValueConversion) {
    CMachineParameter p = MakeParam(pt_byte, 10, 110, 255, 60);
    double norm = ParameterMapping::BuzzToNormalized(110, &p);
    ASSERT_NEAR(norm, 1.0, 0.001);
}

TEST(DeferredParamRestore, RoundtripFullRange) {
    // Test that the manual deferred restore formula matches ParameterMapping
    CMachineParameter p = MakeParam(pt_byte, 28, 228, 255, 128);
    int stepCount = p.MaxValue - p.MinValue;
    for (int v = p.MinValue; v <= p.MaxValue; v++) {
        double manual = (double)(v - p.MinValue) / (double)stepCount;
        double lib = ParameterMapping::BuzzToNormalized(v, &p);
        ASSERT_NEAR(manual, lib, 0.0001);

        // Back to Buzz should match
        int back = ParameterMapping::NormalizedToBuzz(lib, &p);
        ASSERT_EQ(back, v);
    }
}

// ============================================================================
// 8. BuzzParamLayout edge cases (new)
// ============================================================================

TEST(ParamLayoutNew, ZeroGlobalWithTrackParams) {
    // Machine with 0 global params and only track params (like WhiteNoise's Looper 2)
    CMachineParameter pVol = MakeParam(pt_byte, 0, 254, 255, 128);
    CMachineParameter pWave = MakeParam(pt_byte, 1, 255, 0, 1);
    CMachineParameter pLen = MakeParam(pt_word, 1, 4096, 0, 16);

    const CMachineParameter* params[] = { &pVol, &pWave, &pLen };
    CMachineInfo info = {};
    info.numGlobalParameters = 0;
    info.numTrackParameters = 3;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    ASSERT_EQ(layout.GetGlobalSlots().size(), 0u);
    ASSERT_EQ(layout.GetTrackSlots().size(), 3u);
    ASSERT_EQ(layout.GetGlobalStructSize(), 0);
    // 1 + 1 + 2 = 4 bytes per track
    ASSERT_EQ(layout.GetTrackStructSize(), 4);
}

TEST(ParamLayoutNew, WriteTrackNoValues) {
    CMachineParameter pNote = MakeParam(pt_note, 0, 156, 0, 0);
    CMachineParameter pVel = MakeParam(pt_byte, 0, 240, 255, 224);

    const CMachineParameter* params[] = { &pNote, &pVel };
    CMachineInfo info = {};
    info.numGlobalParameters = 0;
    info.numTrackParameters = 2;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    // 1 + 1 = 2 bytes per track
    char trackBuf[4] = {};
    layout.WriteTrackAllNoValues(trackBuf, 1);

    // pt_note NoValue = 0, pt_byte NoValue = 255
    ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 0), 0);
    ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 1), 255);
}
