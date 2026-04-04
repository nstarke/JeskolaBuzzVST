// Security-focused validation tests for BuzzParamLayout, ParameterMapping,
// and BuzzMachineLoader edge cases.
//
// NOTE: State serialization round-trip tests (BuzzProcessor::getState/setState)
// are omitted because the test binary does not link against BuzzProcessor.cpp
// or the full VST3 SDK components required to instantiate a processor.

#include <windows.h>
#include <climits>
#include "TestFramework.h"
#include "../src/buzz/MachineInterface.h"
#include "../src/buzz/BuzzParamLayout.h"
#include "../src/buzz/BuzzMachineLoader.h"
#include "../src/vst3/ParameterMapping.h"

using namespace BuzzVst;

// --- Helper: create mock CMachineParameter ---
static CMachineParameter MakeParam(CMPType type, int minVal, int maxVal, int noVal, int defVal, int flags = MPF_STATE) {
    CMachineParameter p = {};
    p.Type = type;
    p.Name = "TestParam";
    p.Description = "Test";
    p.MinValue = minVal;
    p.MaxValue = maxVal;
    p.NoValue = noVal;
    p.DefValue = defVal;
    p.Flags = flags;
    return p;
}

// Helper: build a layout with one global byte param and one track byte param
static void BuildSimpleLayout(BuzzParamLayout& layout,
                              CMachineParameter& gp,
                              CMachineParameter& tp,
                              const CMachineParameter* params[2],
                              CMachineInfo& info) {
    gp = MakeParam(pt_byte, 0, 127, 255, 64);
    tp = MakeParam(pt_byte, 0, 127, 255, 80);
    params[0] = &gp;
    params[1] = &tp;

    info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 1;
    info.numTrackParameters = 1;
    info.Parameters = params;
    info.minTracks = 1;
    info.maxTracks = 4;

    layout.Build(&info);
}

// ===== ParamLayout: WriteTrackParam with negative trackIndex =====

TEST(Validation, WriteTrackParamNegativeTrackIndex) {
    BuzzParamLayout layout;
    CMachineParameter gp, tp;
    const CMachineParameter* params[2];
    CMachineInfo info;
    BuildSimpleLayout(layout, gp, tp, params, info);

    unsigned char trackBuf[16] = {};
    // Should not crash, should be silently ignored
    layout.WriteTrackParam(trackBuf, -1, 0, 42);
    layout.WriteTrackParam(trackBuf, -100, 0, 42);
    layout.WriteTrackParam(trackBuf, INT_MIN, 0, 42);
    // Buffer should be unchanged
    ASSERT_EQ(trackBuf[0], 0);
}

// ===== ParamLayout: WriteTrackParam with huge trackIndex (overflow) =====

TEST(Validation, WriteTrackParamHugeTrackIndex) {
    // Build a layout with a word track param (trackStructSize = 2)
    // so that INT_MAX / 2 is a meaningful threshold.
    CMachineParameter gp = MakeParam(pt_byte, 0, 127, 255, 64);
    CMachineParameter tp = MakeParam(pt_word, 0, 65535, 65535, 1000);
    const CMachineParameter* params[] = { &gp, &tp };

    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 1;
    info.numTrackParameters = 1;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    unsigned char trackBuf[16] = {};
    // INT_MAX with trackStructSize=2 would overflow; should return early
    layout.WriteTrackParam(trackBuf, INT_MAX, 0, 42);
    // INT_MAX / 2 + 1 also overflows
    layout.WriteTrackParam(trackBuf, INT_MAX / 2 + 1, 0, 42);
    ASSERT_EQ(trackBuf[0], 0);
}

// ===== ParamLayout: WriteTrackParam with trackStructSize causing overflow =====

TEST(Validation, WriteTrackParamOverflowCheck) {
    // Build a layout with a known trackStructSize, then test that
    // trackIndex * trackStructSize overflow is caught.
    CMachineParameter gp = MakeParam(pt_byte, 0, 127, 255, 64);
    CMachineParameter tp = MakeParam(pt_word, 0, 65535, 65535, 1000);
    const CMachineParameter* params[] = { &gp, &tp };

    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 1;
    info.numTrackParameters = 1;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    // trackStructSize is 2 (one word param).
    // INT_MAX / 2 is within bounds mathematically, but INT_MAX/2 + 1 would overflow.
    // The check is: trackIndex > INT_MAX / trackStructSize
    // So trackIndex = INT_MAX should be rejected.
    unsigned char trackBuf[16] = {};
    layout.WriteTrackParam(trackBuf, INT_MAX, 0, 42);
    // Should not crash; buffer should be untouched
    ASSERT_EQ(trackBuf[0], 0);
}

// ===== ParamLayout: WriteGlobalParam with slotIndex beyond globalSlots =====

TEST(Validation, WriteGlobalParamBeyondSlots) {
    CMachineParameter p1 = MakeParam(pt_byte, 0, 127, 255, 64);
    const CMachineParameter* params[] = { &p1 };

    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 1;
    info.numTrackParameters = 0;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    unsigned char buf[16] = {};
    // slotIndex == 1, but only 1 slot exists (index 0)
    layout.WriteGlobalParam(buf, 1, 42);
    layout.WriteGlobalParam(buf, 100, 42);
    layout.WriteGlobalParam(buf, INT_MAX, 42);
    ASSERT_EQ(buf[0], 0); // unchanged
}

// ===== ParamLayout: ReadTrackParam with invalid indices =====

TEST(Validation, ReadTrackParamInvalidIndices) {
    // Build a layout with a word track param (trackStructSize = 2)
    // so overflow checks are meaningful.
    CMachineParameter gp = MakeParam(pt_byte, 0, 127, 255, 64);
    CMachineParameter tp = MakeParam(pt_word, 0, 65535, 65535, 1000);
    const CMachineParameter* params[] = { &gp, &tp };

    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 1;
    info.numTrackParameters = 1;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    unsigned char trackBuf[16] = {};
    trackBuf[0] = 42; // write a known value at offset 0

    // Negative trackIndex
    ASSERT_EQ(layout.ReadTrackParam(trackBuf, -1, 0), 0);
    // Huge trackIndex that would overflow with trackStructSize=2
    ASSERT_EQ(layout.ReadTrackParam(trackBuf, INT_MAX, 0), 0);
    ASSERT_EQ(layout.ReadTrackParam(trackBuf, INT_MAX / 2 + 1, 0), 0);
    // Negative slotIndex
    ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, -1), 0);
    // slotIndex beyond track slots
    ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 100), 0);
    // null buffer
    ASSERT_EQ(layout.ReadTrackParam(nullptr, 0, 0), 0);
}

// ===== ParameterMapping: NormalizedToBuzz with value slightly below 0 =====

TEST(Validation, NormalizedToBuzzSlightlyBelowZero) {
    CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);
    // Should clamp to min (0)
    ASSERT_EQ(ParameterMapping::NormalizedToBuzz(-0.001, &p), 0);
    ASSERT_EQ(ParameterMapping::NormalizedToBuzz(-1e-10, &p), 0);
}

// ===== ParameterMapping: NormalizedToBuzz with value slightly above 1 =====

TEST(Validation, NormalizedToBuzzSlightlyAboveOne) {
    CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);
    // Should clamp to max (127)
    ASSERT_EQ(ParameterMapping::NormalizedToBuzz(1.001, &p), 127);
    ASSERT_EQ(ParameterMapping::NormalizedToBuzz(1.0 + 1e-10, &p), 127);
}

// ===== ParameterMapping: BuzzToNormalized with value below min =====

TEST(Validation, BuzzToNormalizedBelowMin) {
    CMachineParameter p = MakeParam(pt_byte, 10, 110, 255, 60);
    double norm = ParameterMapping::BuzzToNormalized(5, &p);
    // Should clamp to 0.0
    ASSERT_NEAR(norm, 0.0, 0.001);
}

// ===== BuzzMachineLoader: Unload without loading first =====

TEST(Validation, UnloadWithoutLoading) {
    BuzzMachineLoader loader;
    // Should not crash
    loader.Unload();
    ASSERT_FALSE(loader.IsLoaded());
    ASSERT_NULL(loader.GetMachine());
    ASSERT_NULL(loader.GetInfo());
}

// ===== BuzzMachineLoader: Double unload =====

TEST(Validation, DoubleUnload) {
    BuzzMachineLoader loader;
    // First unload on a never-loaded loader
    loader.Unload();
    // Second unload - should also not crash
    loader.Unload();
    ASSERT_FALSE(loader.IsLoaded());
}
