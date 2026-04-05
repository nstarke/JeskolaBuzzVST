// Tests for BridgeIPC helpers, BuzzSampleToVst3 conversion, DescribeValue protocol,
// wave table boundary conditions, and parameter persistence data flow.

#include "TestFramework.h"
#include "TestHelpers.h"
#include "../src/bridge/BridgeIPC.h"
#include "../src/vst3/plugids.h"
#include "../src/buzz/BuzzWaveTable.h"
#include "../src/buzz/BuzzParamLayout.h"
#include "../src/buzz/BuzzMachineLoader.h"
#include "../src/buzz/BuzzMDKHelper.h"
#include "../src/buzz/BuzzOscTables.h"
#include "../src/buzz/BuzzPresetLoader.h"
#include "../src/vst3/ParameterMapping.h"
#include "../src/vst3/GearScanner.h"
#include <cstring>
#include <cmath>
#include <limits>
#include <fstream>

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

// MakeParam is in TestHelpers.h

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

// ============================================================================
// 9. ValidateInfo edge cases (BuzzMachineLoader)
// ============================================================================

TEST(ValidateInfo, NullInfoRejected) {
    BuzzMachineLoader loader;
    // ValidateInfo is private, but Load with a bad path exercises it.
    // We test via observable behavior: Load returns false for invalid DLLs.
    ASSERT_FALSE(loader.Load(""));
    ASSERT_FALSE(loader.Load(nullptr));
}

TEST(ValidateInfo, NegativeParamCounts) {
    // CMachineInfo with negative param counts should fail validation.
    // We can't call ValidateInfo directly, but we verify via the public
    // Build function that handles these gracefully.
    CMachineParameter pByte = MakeParam(pt_byte, 0, 127, 255, 64);
    const CMachineParameter* params[] = { &pByte };

    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = -1;
    info.numTrackParameters = 0;
    info.Parameters = params;
    info.minTracks = 0;
    info.maxTracks = 0;

    // Build should handle negative counts gracefully
    BuzzParamLayout layout;
    layout.Build(&info);
    // Negative count treated as 0 — no crash
    ASSERT_EQ(layout.GetGlobalSlots().size(), 0u);
}

TEST(ValidateInfo, MaxTracksLessThanMinTracks) {
    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 0;
    info.numTrackParameters = 0;
    info.Parameters = nullptr;
    info.minTracks = 5;
    info.maxTracks = 2; // invalid: max < min

    // Build should not crash with bad track counts
    BuzzParamLayout layout;
    layout.Build(&info);
    ASSERT_EQ(layout.GetGlobalSlots().size(), 0u);
}

// ============================================================================
// 10. Oscillator table waveform verification
// ============================================================================

TEST(OscTables, SineIsZeroAtStart) {
    const short* sine = BuzzOscTables::GetTable(OWF_SINE);
    ASSERT_NOT_NULL(sine);
    // Level 0 starts at offset 0, 2048 samples. Sine should start near 0.
    ASSERT_LE(abs((int)sine[0]), 100); // near zero at phase 0
}

TEST(OscTables, SineHasFullAmplitude) {
    const short* sine = BuzzOscTables::GetTable(OWF_SINE);
    ASSERT_NOT_NULL(sine);
    // Find max in level 0 (2048 samples). Should be near 32767.
    int maxVal = 0;
    for (int i = 0; i < 2048; i++) {
        int v = abs((int)sine[i]);
        if (v > maxVal) maxVal = v;
    }
    ASSERT_GT(maxVal, 30000);
}

TEST(OscTables, SawtoothMaxAmplitude) {
    const short* saw = BuzzOscTables::GetTable(OWF_SAWTOOTH);
    ASSERT_NOT_NULL(saw);
    int maxVal = 0;
    for (int i = 0; i < 2048; i++) {
        int v = abs((int)saw[i]);
        if (v > maxVal) maxVal = v;
    }
    ASSERT_GT(maxVal, 20000);
}

TEST(OscTables, PulseIsSymmetric) {
    const short* pulse = BuzzOscTables::GetTable(OWF_PULSE);
    ASSERT_NOT_NULL(pulse);
    // Pulse wave should have positive and negative regions
    bool hasPositive = false, hasNegative = false;
    for (int i = 0; i < 2048; i++) {
        if (pulse[i] > 1000) hasPositive = true;
        if (pulse[i] < -1000) hasNegative = true;
    }
    ASSERT_TRUE(hasPositive);
    ASSERT_TRUE(hasNegative);
}

TEST(OscTables, TriangleMaxAmplitude) {
    const short* tri = BuzzOscTables::GetTable(OWF_TRIANGLE);
    ASSERT_NOT_NULL(tri);
    int maxVal = 0;
    for (int i = 0; i < 2048; i++) {
        int v = abs((int)tri[i]);
        if (v > maxVal) maxVal = v;
    }
    ASSERT_GT(maxVal, 20000);
}

TEST(OscTables, NoiseIsNonZero) {
    const short* noise = BuzzOscTables::GetTable(OWF_NOISE);
    ASSERT_NOT_NULL(noise);
    // Noise should have some non-zero values
    int nonZero = 0;
    for (int i = 0; i < 2048; i++) {
        if (noise[i] != 0) nonZero++;
    }
    ASSERT_GT(nonZero, 100);
}

TEST(OscTables, Sawtooth303Exists) {
    const short* saw303 = BuzzOscTables::GetTable(OWF_303_SAWTOOTH);
    ASSERT_NOT_NULL(saw303);
    int maxVal = 0;
    for (int i = 0; i < 2048; i++) {
        int v = abs((int)saw303[i]);
        if (v > maxVal) maxVal = v;
    }
    ASSERT_GT(maxVal, 10000);
}

TEST(OscTables, HigherLevelsAreShorter) {
    // GetOscTblOffset gives the start offset for each level.
    // Level 0 = 0, Level 1 = 2048, Level 2 = 2048+1024, etc.
    int off0 = GetOscTblOffset(0);
    int off1 = GetOscTblOffset(1);
    int off2 = GetOscTblOffset(2);
    ASSERT_EQ(off0, 0);
    ASSERT_EQ(off1, 2048);
    ASSERT_EQ(off2, 2048 + 1024);
    // Each subsequent level should have a higher offset
    for (int lvl = 1; lvl <= 10; lvl++) {
        ASSERT_GT(GetOscTblOffset(lvl), GetOscTblOffset(lvl - 1));
    }
}

// ============================================================================
// 11. MDK stub lifecycle
// ============================================================================

TEST(MDKStub, CreateAndDestroy) {
    void* stub = CreateMDKStub();
    ASSERT_NOT_NULL(stub);
    ASSERT_FALSE(WasMDKInitCalled(stub));
    DestroyMDKStub(stub);
}

TEST(MDKStub, InitCalledFlag) {
    void* stub = CreateMDKStub();
    ASSERT_NOT_NULL(stub);
    ASSERT_FALSE(WasMDKInitCalled(stub));

    // Simulate vtable[10] call by setting the flag directly
    // (We can't call through vtable in unit test without a real machine)
    // Instead, test the Reset function
    ResetMDKInitFlag(stub);
    ASSERT_FALSE(WasMDKInitCalled(stub));

    DestroyMDKStub(stub);
}

TEST(MDKStub, NullSafety) {
    ASSERT_FALSE(WasMDKInitCalled(nullptr));
    ResetMDKInitFlag(nullptr); // should not crash
    DestroyMDKStub(nullptr);   // should not crash
}

// ============================================================================
// 12. Trigger switch detection (pt_switch with min=1 max=1)
// ============================================================================

TEST(TriggerSwitch, DetectTrigParam) {
    // A pt_switch with min=1, max=1 is a trigger (like ErsBlipp's Trig)
    CMachineParameter pTrig = MakeParam(pt_switch, 1, 1, 255, 255, 0);
    ASSERT_EQ(pTrig.Type, (CMPType)pt_switch);
    ASSERT_EQ(pTrig.MinValue, 1);
    ASSERT_EQ(pTrig.MaxValue, 1);
}

TEST(TriggerSwitch, NotATrigger) {
    // A normal pt_switch (min=0, max=1) is NOT a trigger
    CMachineParameter pSwitch = MakeParam(pt_switch, 0, 1, 255, 0, MPF_STATE);
    ASSERT_FALSE(pSwitch.MinValue == 1 && pSwitch.MaxValue == 1);
}

TEST(TriggerSwitch, TriggerLayout) {
    // Machine with only track params including a trigger
    CMachineParameter pFreq = MakeParam(pt_word, 0, 32768, 65535, 5825);
    CMachineParameter pGain = MakeParam(pt_byte, 0, 127, 255, 113);
    CMachineParameter pTrig = MakeParam(pt_switch, 1, 1, 255, 255, 0);

    const CMachineParameter* params[] = { &pFreq, &pGain, &pTrig };
    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 0;
    info.numTrackParameters = 3;
    info.Parameters = params;
    info.minTracks = 1;
    info.maxTracks = 1;

    BuzzParamLayout layout;
    layout.Build(&info);

    auto& tSlots = layout.GetTrackSlots();
    ASSERT_EQ(tSlots.size(), 3u);

    // Find trigger slot
    int trigSlot = -1;
    for (int i = 0; i < (int)tSlots.size(); i++) {
        if (tSlots[i].param->Type == pt_switch &&
            tSlots[i].param->MinValue == 1 && tSlots[i].param->MaxValue == 1) {
            trigSlot = i;
            break;
        }
    }
    ASSERT_EQ(trigSlot, 2);

    // Verify we can write trigger value
    // Track struct: word(2) + byte(1) + switch(1) = 4 bytes
    ASSERT_EQ(layout.GetTrackStructSize(), 4);
    char buf[8] = {};
    layout.WriteTrackAllNoValues(buf, 1);
    layout.WriteTrackParam(buf, 0, trigSlot, 1);
    ASSERT_EQ(layout.ReadTrackParam(buf, 0, trigSlot), 1);
}

// ============================================================================
// 13. BuzzParamLayout Build with edge cases
// ============================================================================

TEST(ParamLayoutEdge, NullInfoPointer) {
    BuzzParamLayout layout;
    layout.Build(nullptr);
    ASSERT_EQ(layout.GetGlobalSlots().size(), 0u);
    ASSERT_EQ(layout.GetTrackSlots().size(), 0u);
}

TEST(ParamLayoutEdge, ZeroParamsNullParameters) {
    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 0;
    info.numTrackParameters = 0;
    info.Parameters = nullptr;

    BuzzParamLayout layout;
    layout.Build(&info);
    ASSERT_EQ(layout.GetGlobalSlots().size(), 0u);
    ASSERT_EQ(layout.GetTrackSlots().size(), 0u);
    ASSERT_EQ(layout.GetGlobalStructSize(), 0);
    ASSERT_EQ(layout.GetTrackStructSize(), 0);
}

TEST(ParamLayoutEdge, SingleNoteParam) {
    CMachineParameter pNote = MakeParam(pt_note, 1, 156, 0, 0);
    const CMachineParameter* params[] = { &pNote };
    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 0;
    info.numTrackParameters = 1;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    auto& tSlots = layout.GetTrackSlots();
    ASSERT_EQ(tSlots.size(), 1u);
    ASSERT_EQ(tSlots[0].param->Type, (CMPType)pt_note);
    // pt_note = 1 byte
    ASSERT_EQ(layout.GetTrackStructSize(), 1);
}

// ============================================================================
// 14. Wave table auto-slot and GetLoadedPaths
// ============================================================================

TEST(WaveTableAuto, GetLoadedPathsEmpty) {
    BuzzWaveTable wt;
    auto paths = wt.GetLoadedPaths();
    ASSERT_EQ(paths.size(), 0u);
}

TEST(WaveTableAuto, ClearAllSlots) {
    BuzzWaveTable wt;
    wt.ClearAll();
    for (int i = 1; i <= 200; i++) {
        ASSERT_FALSE(wt.IsLoaded(i));
    }
    ASSERT_EQ(wt.GetFreeWave(), 1);
}

// ============================================================================
// 15. Preset loader
// ============================================================================

TEST(PresetLoader, FindPrsForDllNoFile) {
    std::string prs = BuzzPresetLoader::FindPrsForDll(
        "C:\\nonexistent\\path\\machine.dll");
    ASSERT_TRUE(prs.empty());
}

TEST(PresetLoader, FindPrsEmptyPath) {
    ASSERT_TRUE(BuzzPresetLoader::FindPrsForDll("").empty());
    ASSERT_TRUE(BuzzPresetLoader::FindPrsForDll("abc").empty());
}

TEST(PresetLoader, LoadNonexistent) {
    BuzzPresetLoader loader;
    ASSERT_FALSE(loader.Load("C:\\nonexistent\\file.prs"));
    ASSERT_EQ(loader.GetPresets().size(), 0u);
}

TEST(PresetLoader, LoadErsBlippPresets) {
    std::string prs = BuzzPresetLoader::FindPrsForDll(
        GetGearPath("generators\\ErsBlipp.dll"));
    if (prs.empty()) return; // skip if not present

    BuzzPresetLoader loader;
    ASSERT_TRUE(loader.Load(prs));
    ASSERT_GT(loader.GetPresets().size(), 0u);
    ASSERT_EQ(loader.GetMachineName(), std::string("ErsBlipp"));

    for (auto& p : loader.GetPresets()) {
        ASSERT_FALSE(p.name.empty());
        ASSERT_GT(p.paramValues.size(), 0u);
    }
}

TEST(PresetLoader, LoadCyanPhasePresets) {
    std::string prs = BuzzPresetLoader::FindPrsForDll(
        GetGearPath("generators\\CyanPhase Slide Flute.dll"));
    if (prs.empty()) return;

    BuzzPresetLoader loader;
    ASSERT_TRUE(loader.Load(prs));

    bool hasComment = false;
    for (auto& p : loader.GetPresets()) {
        if (!p.comment.empty()) hasComment = true;
    }
    ASSERT_TRUE(hasComment);
}

TEST(PresetLoader, ClearResets) {
    BuzzPresetLoader loader;
    std::string prs = BuzzPresetLoader::FindPrsForDll(
        GetGearPath("generators\\ErsBlipp.dll"));
    if (!prs.empty()) loader.Load(prs);
    loader.Clear();
    ASSERT_EQ(loader.GetPresets().size(), 0u);
    ASSERT_TRUE(loader.GetMachineName().empty());
}

TEST(PresetLoader, PresetParamCountMatchesStateParams) {
    // ErsBlipp has 6 track params, 5 with MPF_STATE. Presets should have 5 values.
    std::string prs = BuzzPresetLoader::FindPrsForDll(
        GetGearPath("generators\\ErsBlipp.dll"));
    if (prs.empty()) return;

    BuzzPresetLoader loader;
    loader.Load(prs);
    for (auto& p : loader.GetPresets()) {
        ASSERT_EQ(p.paramValues.size(), 5u);
    }
}

// ============================================================================
// 16. Preset save and roundtrip
// ============================================================================

TEST(PresetSave, SaveAndReload) {
    // Create a preset, save to temp file, reload and verify
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string tempPrs = std::string(tempDir) + "buzzbridge_test_preset.prs";

    BuzzPresetLoader saver;
    saver.SetMachineName("TestMachine");

    BuzzPreset p1;
    p1.name = "Bright Lead";
    p1.paramValues = {100, 200, 50, 128};
    p1.comment = "A bright lead sound";
    saver.AddPreset(p1);

    BuzzPreset p2;
    p2.name = "Soft Pad";
    p2.paramValues = {30, 180, 90, 64};
    saver.AddPreset(p2);

    ASSERT_TRUE(saver.Save(tempPrs));

    // Reload
    BuzzPresetLoader loader;
    ASSERT_TRUE(loader.Load(tempPrs));
    ASSERT_EQ(loader.GetMachineName(), std::string("TestMachine"));
    ASSERT_EQ(loader.GetPresets().size(), 2u);

    ASSERT_EQ(loader.GetPresets()[0].name, std::string("Bright Lead"));
    ASSERT_EQ(loader.GetPresets()[0].paramValues.size(), 4u);
    ASSERT_EQ(loader.GetPresets()[0].paramValues[0], 100);
    ASSERT_EQ(loader.GetPresets()[0].paramValues[3], 128);
    ASSERT_EQ(loader.GetPresets()[0].comment, std::string("A bright lead sound"));

    ASSERT_EQ(loader.GetPresets()[1].name, std::string("Soft Pad"));
    ASSERT_EQ(loader.GetPresets()[1].paramValues[0], 30);
    ASSERT_TRUE(loader.GetPresets()[1].comment.empty());

    // Cleanup
    DeleteFileA(tempPrs.c_str());
}

TEST(PresetSave, AppendToExisting) {
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string tempPrs = std::string(tempDir) + "buzzbridge_test_append.prs";

    // Save initial preset
    BuzzPresetLoader saver;
    saver.SetMachineName("AppendTest");
    BuzzPreset p1;
    p1.name = "First";
    p1.paramValues = {10, 20};
    saver.AddPreset(p1);
    ASSERT_TRUE(saver.Save(tempPrs));

    // Reload, add another, save again
    BuzzPresetLoader loader;
    ASSERT_TRUE(loader.Load(tempPrs));
    ASSERT_EQ(loader.GetPresets().size(), 1u);

    BuzzPreset p2;
    p2.name = "Second";
    p2.paramValues = {30, 40};
    loader.AddPreset(p2);
    ASSERT_TRUE(loader.Save(tempPrs));

    // Reload and verify both are there
    BuzzPresetLoader final;
    ASSERT_TRUE(final.Load(tempPrs));
    ASSERT_EQ(final.GetPresets().size(), 2u);
    ASSERT_EQ(final.GetPresets()[0].name, std::string("First"));
    ASSERT_EQ(final.GetPresets()[1].name, std::string("Second"));

    DeleteFileA(tempPrs.c_str());
}

TEST(PresetSave, SaveEmptyFails) {
    BuzzPresetLoader saver;
    // No machine name set
    ASSERT_FALSE(saver.Save("C:\\temp\\test.prs"));
}

TEST(PresetSave, PrsPathForDll) {
    ASSERT_EQ(BuzzPresetLoader::PrsPathForDll("C:\\Gear\\Machine.dll"),
              std::string("C:\\Gear\\Machine.prs"));
    ASSERT_EQ(BuzzPresetLoader::PrsPathForDll("foo.dll"),
              std::string("foo.prs"));
    ASSERT_TRUE(BuzzPresetLoader::PrsPathForDll("").empty());
    ASSERT_TRUE(BuzzPresetLoader::PrsPathForDll("abc").empty());
}

TEST(PresetSave, RoundtripWithComment) {
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string tempPrs = std::string(tempDir) + "buzzbridge_test_comment.prs";

    BuzzPresetLoader saver;
    saver.SetMachineName("CommentTest");
    BuzzPreset p;
    p.name = "WithComment";
    p.paramValues = {42};
    p.comment = "This has a comment";
    saver.AddPreset(p);

    BuzzPreset p2;
    p2.name = "NoComment";
    p2.paramValues = {99};
    saver.AddPreset(p2);
    ASSERT_TRUE(saver.Save(tempPrs));

    BuzzPresetLoader loader;
    ASSERT_TRUE(loader.Load(tempPrs));
    ASSERT_EQ(loader.GetPresets()[0].comment, std::string("This has a comment"));
    ASSERT_TRUE(loader.GetPresets()[1].comment.empty());

    DeleteFileA(tempPrs.c_str());
}

// ============================================================================
// 17. Preset validation
// ============================================================================

TEST(PresetValidation, RejectTruncatedFile) {
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string tempPrs = std::string(tempDir) + "buzzbridge_test_truncated.prs";

    // Write just the version and a partial machine name length
    std::ofstream f(tempPrs, std::ios::binary);
    uint32_t version = 1;
    f.write((char*)&version, 4);
    uint32_t nameLen = 100; // claims 100 bytes but file ends here
    f.write((char*)&nameLen, 4);
    f.close();

    BuzzPresetLoader loader;
    ASSERT_FALSE(loader.Load(tempPrs));
    ASSERT_EQ(loader.GetPresets().size(), 0u);

    DeleteFileA(tempPrs.c_str());
}

TEST(PresetValidation, RejectBadVersion) {
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string tempPrs = std::string(tempDir) + "buzzbridge_test_badver.prs";

    std::ofstream f(tempPrs, std::ios::binary);
    uint32_t version = 99;
    f.write((char*)&version, 4);
    f.close();

    BuzzPresetLoader loader;
    ASSERT_FALSE(loader.Load(tempPrs));

    DeleteFileA(tempPrs.c_str());
}

TEST(PresetValidation, RejectHugePresetCount) {
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string tempPrs = std::string(tempDir) + "buzzbridge_test_hugecount.prs";

    std::ofstream f(tempPrs, std::ios::binary);
    uint32_t v;
    v = 1; f.write((char*)&v, 4);
    v = 4; f.write((char*)&v, 4);
    f.write("Test", 4);
    v = 999999; f.write((char*)&v, 4); // way too many presets
    f.close();

    BuzzPresetLoader loader;
    ASSERT_FALSE(loader.Load(tempPrs));

    DeleteFileA(tempPrs.c_str());
}

TEST(PresetValidation, RejectHugeParamCount) {
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string tempPrs = std::string(tempDir) + "buzzbridge_test_hugeparams.prs";

    std::ofstream f(tempPrs, std::ios::binary);
    uint32_t v;
    v = 1; f.write((char*)&v, 4);
    v = 4; f.write((char*)&v, 4);
    f.write("Test", 4);
    v = 1; f.write((char*)&v, 4);      // 1 preset
    v = 3; f.write((char*)&v, 4);      // name len
    f.write("Bad", 3);
    v = 1; f.write((char*)&v, 4);      // numTracks
    v = 99999; f.write((char*)&v, 4);  // numParams too large
    f.close();

    BuzzPresetLoader loader;
    ASSERT_FALSE(loader.Load(tempPrs));

    DeleteFileA(tempPrs.c_str());
}

TEST(PresetValidation, RejectEmptyFile) {
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string tempPrs = std::string(tempDir) + "buzzbridge_test_empty.prs";

    std::ofstream f(tempPrs, std::ios::binary);
    f.close();

    BuzzPresetLoader loader;
    ASSERT_FALSE(loader.Load(tempPrs));

    DeleteFileA(tempPrs.c_str());
}

TEST(PresetValidation, ControlCharsStripped) {
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    std::string tempPrs = std::string(tempDir) + "buzzbridge_test_ctrlchars.prs";

    BuzzPresetLoader saver;
    saver.SetMachineName("TestMachine");
    BuzzPreset p;
    p.name = "Test\x01\x02Name";
    p.paramValues = {42};
    saver.AddPreset(p);
    saver.Save(tempPrs);

    BuzzPresetLoader loader;
    loader.Load(tempPrs);
    if (!loader.GetPresets().empty()) {
        auto& name = loader.GetPresets()[0].name;
        for (char c : name) {
            ASSERT_TRUE(c >= 0x20 || c == '\t' || c < 0);
        }
    }

    DeleteFileA(tempPrs.c_str());
}

// ============================================================================
// 18. BridgePipe real I/O tests (named pipe roundtrip)
// ============================================================================

TEST(BridgePipeIO, WriteReadRoundtrip) {
    std::string pipeName = "\\\\.\\pipe\\BuzzBridgeTest_PipeIO";
    HANDLE hServer = CreateNamedPipeA(pipeName.c_str(),
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, nullptr);
    ASSERT_NE(hServer, INVALID_HANDLE_VALUE);

    HANDLE hClient = CreateFileA(pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(hClient, INVALID_HANDLE_VALUE);
    ConnectNamedPipe(hServer, nullptr);

    BridgePipe server, client;
    server.SetHandle(hServer);
    client.SetHandle(hClient);

    // Client sends command, server reads it
    ASSERT_TRUE(client.SendCommand(kCmdPing));
    BridgeCmdHeader cmd = {};
    ASSERT_TRUE(server.ReadCommand(cmd));
    ASSERT_EQ(cmd.cmd, kCmdPing);
    ASSERT_EQ(cmd.payloadSize, 0u);

    // Server sends response, client reads it
    ASSERT_TRUE(server.SendResponse(kRespOk));
    BridgeRespHeader resp = {};
    ASSERT_TRUE(client.ReadResponse(resp));
    ASSERT_EQ(resp.resp, kRespOk);

    server.Close();
    client.Close();
}

TEST(BridgePipeIO, CommandWithPayload) {
    std::string pipeName = "\\\\.\\pipe\\BuzzBridgeTest_Payload";
    HANDLE hServer = CreateNamedPipeA(pipeName.c_str(),
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, nullptr);
    ASSERT_NE(hServer, INVALID_HANDLE_VALUE);
    HANDLE hClient = CreateFileA(pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(hClient, INVALID_HANDLE_VALUE);
    ConnectNamedPipe(hServer, nullptr);

    BridgePipe server, client;
    server.SetHandle(hServer);
    client.SetHandle(hClient);

    const char* path = "C:\\test\\machine.dll";
    uint32_t pathLen = (uint32_t)strlen(path);
    ASSERT_TRUE(client.SendCommand(kCmdLoadDll, path, pathLen));

    BridgeCmdHeader cmd = {};
    ASSERT_TRUE(server.ReadCommand(cmd));
    ASSERT_EQ(cmd.cmd, kCmdLoadDll);
    ASSERT_EQ(cmd.payloadSize, pathLen);

    char buf[256] = {};
    ASSERT_TRUE(server.ReadAll(buf, cmd.payloadSize));
    ASSERT_EQ(std::string(buf, cmd.payloadSize), std::string(path));

    server.Close();
    client.Close();
}

TEST(BridgePipeIO, MasterInfoRoundtrip) {
    std::string pipeName = "\\\\.\\pipe\\BuzzBridgeTest_MasterInfo";
    HANDLE hServer = CreateNamedPipeA(pipeName.c_str(),
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, nullptr);
    ASSERT_NE(hServer, INVALID_HANDLE_VALUE);
    HANDLE hClient = CreateFileA(pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(hClient, INVALID_HANDLE_VALUE);
    ConnectNamedPipe(hServer, nullptr);

    BridgePipe server, client;
    server.SetHandle(hServer);
    client.SetHandle(hClient);

    BridgeMasterInfo mi = { 125, 4, 44100 };
    ASSERT_TRUE(client.SendCommand(kCmdSetMasterInfo, &mi, sizeof(mi)));

    BridgeCmdHeader cmd = {};
    ASSERT_TRUE(server.ReadCommand(cmd));
    ASSERT_EQ(cmd.cmd, kCmdSetMasterInfo);

    BridgeMasterInfo received = {};
    ASSERT_TRUE(server.ReadAll(&received, sizeof(received)));
    ASSERT_EQ(received.beatsPerMin, 125);
    ASSERT_EQ(received.ticksPerBeat, 4);
    ASSERT_EQ(received.samplesPerSec, 44100);

    server.Close();
    client.Close();
}

TEST(BridgePipeIO, TickParamsRoundtrip) {
    std::string pipeName = "\\\\.\\pipe\\BuzzBridgeTest_TickParams";
    HANDLE hServer = CreateNamedPipeA(pipeName.c_str(),
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, nullptr);
    ASSERT_NE(hServer, INVALID_HANDLE_VALUE);
    HANDLE hClient = CreateFileA(pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(hClient, INVALID_HANDLE_VALUE);
    ConnectNamedPipe(hServer, nullptr);

    BridgePipe server, client;
    server.SetHandle(hServer);
    client.SetHandle(hClient);

    std::vector<BridgeTickParam> params = {
        {0, 100}, {5, 200}, {1003, 50}, {-1, 0}
    };
    uint32_t size = (uint32_t)(params.size() * sizeof(BridgeTickParam));
    ASSERT_TRUE(client.SendCommand(kCmdTick, params.data(), size));

    BridgeCmdHeader cmd = {};
    ASSERT_TRUE(server.ReadCommand(cmd));
    ASSERT_EQ(cmd.cmd, kCmdTick);

    std::vector<BridgeTickParam> received(params.size());
    ASSERT_TRUE(server.ReadAll(received.data(), cmd.payloadSize));
    ASSERT_EQ(received[0].paramId, 0);
    ASSERT_EQ(received[0].value, 100);
    ASSERT_EQ(received[2].paramId, 1003);
    ASSERT_EQ(received[3].paramId, -1);

    server.Close();
    client.Close();
}

TEST(BridgePipeIO, ResponseWithPayload) {
    std::string pipeName = "\\\\.\\pipe\\BuzzBridgeTest_RespPayload";
    HANDLE hServer = CreateNamedPipeA(pipeName.c_str(),
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, nullptr);
    ASSERT_NE(hServer, INVALID_HANDLE_VALUE);
    HANDLE hClient = CreateFileA(pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(hClient, INVALID_HANDLE_VALUE);
    ConnectNamedPipe(hServer, nullptr);

    BridgePipe server, client;
    server.SetHandle(hServer);
    client.SetHandle(hClient);

    const char* desc = "Low Pass";
    uint32_t descLen = (uint32_t)strlen(desc);
    ASSERT_TRUE(server.SendResponse(kRespDescribeValue, desc, descLen));

    BridgeRespHeader resp = {};
    ASSERT_TRUE(client.ReadResponse(resp));
    ASSERT_EQ(resp.resp, kRespDescribeValue);
    ASSERT_EQ(resp.payloadSize, descLen);

    char buf[64] = {};
    ASSERT_TRUE(client.ReadAll(buf, resp.payloadSize));
    ASSERT_EQ(std::string(buf, resp.payloadSize), std::string("Low Pass"));

    server.Close();
    client.Close();
}

TEST(BridgePipeIO, InvalidPipeOperationsFail) {
    BridgePipe pipe;
    BridgeCmdHeader cmd = {};
    ASSERT_FALSE(pipe.ReadCommand(cmd));
    ASSERT_FALSE(pipe.SendCommand(kCmdPing));
    BridgeRespHeader resp = {};
    ASSERT_FALSE(pipe.ReadResponse(resp));
    ASSERT_FALSE(pipe.SendResponse(kRespOk));
    char buf[4] = {};
    ASSERT_FALSE(pipe.ReadAll(buf, 4));
    ASSERT_FALSE(pipe.WriteAll(buf, 4));
}

TEST(BridgePipeIO, MidiNoteRoundtrip) {
    std::string pipeName = "\\\\.\\pipe\\BuzzBridgeTest_MidiNote";
    HANDLE hServer = CreateNamedPipeA(pipeName.c_str(),
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, nullptr);
    ASSERT_NE(hServer, INVALID_HANDLE_VALUE);
    HANDLE hClient = CreateFileA(pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(hClient, INVALID_HANDLE_VALUE);
    ConnectNamedPipe(hServer, nullptr);

    BridgePipe server, client;
    server.SetHandle(hServer);
    client.SetHandle(hClient);

    BridgeMidiNote mn = { 0, 60, 100 };
    ASSERT_TRUE(client.SendCommand(kCmdMidiNote, &mn, sizeof(mn)));

    BridgeCmdHeader cmd = {};
    ASSERT_TRUE(server.ReadCommand(cmd));
    ASSERT_EQ(cmd.cmd, kCmdMidiNote);

    BridgeMidiNote received = {};
    ASSERT_TRUE(server.ReadAll(&received, sizeof(received)));
    ASSERT_EQ(received.channel, 0);
    ASSERT_EQ(received.note, 60);
    ASSERT_EQ(received.velocity, 100);

    server.Close();
    client.Close();
}

// ============================================================================
// 19. BridgeSharedMem real tests
// ============================================================================

TEST(BridgeSharedMemIO, CreateAndAccess) {
    BridgeSharedMem mem;
    ASSERT_TRUE(mem.Create("Local\\BuzzBridgeTest_ShmCreate", sizeof(BridgeSharedAudio)));

    auto* audio = mem.GetAudio();
    ASSERT_NOT_NULL(audio);
    ASSERT_EQ(audio->numSamples, 0);
    ASSERT_EQ(audio->hasOutput, 0);
    ASSERT_EQ(audio->outputLeft[0], 0.0f);

    audio->numSamples = 256;
    audio->hasOutput = 1;
    audio->outputLeft[0] = 12345.0f;
    ASSERT_EQ(audio->numSamples, 256);
    ASSERT_EQ(audio->hasOutput, 1);
    ASSERT_EQ(audio->outputLeft[0], 12345.0f);

    mem.Close();
    ASSERT_NULL(mem.GetData());
}

TEST(BridgeSharedMemIO, CreateAndOpenSharesData) {
    std::string name = "Local\\BuzzBridgeTest_ShmShare";

    BridgeSharedMem creator;
    ASSERT_TRUE(creator.Create(name, sizeof(BridgeSharedAudio)));
    auto* a1 = creator.GetAudio();
    ASSERT_NOT_NULL(a1);
    a1->outputLeft[0] = 42.0f;
    a1->numSamples = 128;

    BridgeSharedMem opener;
    ASSERT_TRUE(opener.Open(name, sizeof(BridgeSharedAudio)));
    auto* a2 = opener.GetAudio();
    ASSERT_NOT_NULL(a2);

    // Reads from opener see creator's writes
    ASSERT_EQ(a2->outputLeft[0], 42.0f);
    ASSERT_EQ(a2->numSamples, 128);

    // Writes from opener visible to creator
    a2->outputRight[0] = 99.0f;
    ASSERT_EQ(a1->outputRight[0], 99.0f);

    opener.Close();
    creator.Close();
}

TEST(BridgeSharedMemIO, OpenNonexistentFails) {
    BridgeSharedMem mem;
    ASSERT_FALSE(mem.Open("Local\\BuzzBridgeTest_NoSuchMem", 1024));
    ASSERT_NULL(mem.GetData());
}

TEST(BridgeSharedMemIO, DoubleCloseSafe) {
    BridgeSharedMem mem;
    ASSERT_TRUE(mem.Create("Local\\BuzzBridgeTest_DblClose", 4096));
    mem.Close();
    mem.Close();
    ASSERT_NULL(mem.GetData());
}

TEST(BridgeSharedMemIO, AudioBufferFullWrite) {
    BridgeSharedMem mem;
    ASSERT_TRUE(mem.Create("Local\\BuzzBridgeTest_FullBuf", sizeof(BridgeSharedAudio)));
    auto* audio = mem.GetAudio();
    ASSERT_NOT_NULL(audio);

    // Write full buffer worth of samples
    for (int i = 0; i < kBridgeMaxSamples; i++) {
        audio->outputLeft[i] = (float)i;
        audio->outputRight[i] = (float)(i * 2);
    }
    audio->numSamples = kBridgeMaxSamples;
    audio->hasOutput = 1;

    ASSERT_EQ(audio->outputLeft[0], 0.0f);
    ASSERT_EQ(audio->outputLeft[kBridgeMaxSamples - 1], (float)(kBridgeMaxSamples - 1));
    ASSERT_EQ(audio->outputRight[100], 200.0f);

    mem.Close();
}

// ============================================================================
// 20. Remaining coverage gaps
// ============================================================================

TEST(BuzzCallbacksExtra, GetTPBDefault) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetTPB(), 4);
}

TEST(BuzzCallbacksExtra, GetTPBFromMasterInfo) {
    BuzzCallbacks cb;
    CMasterInfo mi = {};
    mi.TicksPerBeat = 8;
    cb.masterInfoPtr = &mi;
    ASSERT_EQ(cb.GetTPB(), 8);
}

TEST(BuzzCallbacksExtra, GetTempoDefault) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetTempo(), 125);
}

TEST(BuzzCallbacksExtra, GetTempoFromMasterInfo) {
    BuzzCallbacks cb;
    CMasterInfo mi = {};
    mi.BeatsPerMin = 140;
    cb.masterInfoPtr = &mi;
    ASSERT_EQ(cb.GetTempo(), 140);
}

TEST(OscTablesExtra, InitializeIdempotent) {
    BuzzOscTables::Initialize();
    const short* sine1 = BuzzOscTables::GetTable(OWF_SINE);
    ASSERT_NOT_NULL(sine1);
    short saved = sine1[512];

    BuzzOscTables::Initialize();
    const short* sine2 = BuzzOscTables::GetTable(OWF_SINE);
    ASSERT_EQ(sine1, sine2);
    ASSERT_EQ(sine2[512], saved);
}

TEST(OscTablesExtra, AllWaveformsDistinct) {
    const short* tables[BUZZ_NUM_WAVEFORMS];
    for (int w = 0; w < BUZZ_NUM_WAVEFORMS; w++) {
        tables[w] = BuzzOscTables::GetTable(w);
        ASSERT_NOT_NULL(tables[w]);
    }
    for (int a = 0; a < BUZZ_NUM_WAVEFORMS; a++) {
        for (int b = a + 1; b < BUZZ_NUM_WAVEFORMS; b++) {
            int diffs = 0;
            for (int i = 0; i < 2048; i++) {
                if (tables[a][i] != tables[b][i]) diffs++;
            }
            CHECK_TRUE(diffs > 200);
        }
    }
}

TEST(GearScannerExtra, GeneratorTypeIsCorrect) {
    GearScanner scanner;
    std::string gearDir = GetGearPath("");
    if (gearDir.empty()) return;
    while (!gearDir.empty() && (gearDir.back() == '\\' || gearDir.back() == '/'))
        gearDir.pop_back();
    if (!scanner.Scan(gearDir)) return;

    for (auto& g : scanner.GetGenerators()) {
        CHECK_TRUE(g.machineType == MT_GENERATOR);
        CHECK_TRUE(!g.dllPath.empty());
        CHECK_TRUE(!g.displayName.empty());
    }
}

TEST(GearScannerExtra, EffectTypeIsCorrect) {
    GearScanner scanner;
    std::string gearDir = GetGearPath("");
    if (gearDir.empty()) return;
    while (!gearDir.empty() && (gearDir.back() == '\\' || gearDir.back() == '/'))
        gearDir.pop_back();
    if (!scanner.Scan(gearDir)) return;

    for (auto& f : scanner.GetEffects()) {
        CHECK_TRUE(f.machineType == MT_EFFECT);
        CHECK_TRUE(!f.dllPath.empty());
        CHECK_TRUE(!f.displayName.empty());
    }
}

TEST(GearScannerExtra, GeneratorAndEffectCountsSumToTotal) {
    GearScanner scanner;
    std::string gearDir = GetGearPath("");
    if (gearDir.empty()) return;
    while (!gearDir.empty() && (gearDir.back() == '\\' || gearDir.back() == '/'))
        gearDir.pop_back();
    if (!scanner.Scan(gearDir)) return;

    auto total = scanner.GetEntries().size();
    auto gens = scanner.GetGenerators().size();
    auto fxs = scanner.GetEffects().size();
    ASSERT_EQ(gens + fxs, total);
}
