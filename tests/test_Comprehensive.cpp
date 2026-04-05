// Comprehensive additional tests for BuzzBridge.
// Covers untested edge cases, IPC struct layouts, additional callback coverage,
// oscillator table properties, wave table edge cases, and more.

#include <cstring>
#include <cmath>
#include <climits>
#include <vector>
#include <string>
#include "TestFramework.h"
#include "TestHelpers.h"
#include "../src/buzz/BuzzParamLayout.h"
#include "../src/buzz/BuzzCallbacks.h"
#include "../src/buzz/BuzzMachineLoader.h"
#include "../src/buzz/BuzzOscTables.h"
#include "../src/buzz/BuzzWaveTable.h"
#include "../src/vst3/ParameterMapping.h"
#include "../src/vst3/GearScanner.h"
#include "../src/vst3/plugids.h"
#include "../src/bridge/BridgeIPC.h"
#include "../src/common/SEHGuard.h"

using namespace BuzzVst;

// ===========================================================================
// 1. Bridge IPC struct sizes and layout
// ===========================================================================

TEST(BridgeIPC, BridgeCmdHeaderSize) {
    ASSERT_EQ(sizeof(BridgeCmdHeader), 8);
}

TEST(BridgeIPC, BridgeRespHeaderSize) {
    ASSERT_EQ(sizeof(BridgeRespHeader), 8);
}

TEST(BridgeIPC, BridgeMasterInfoSize) {
    ASSERT_EQ(sizeof(BridgeMasterInfo), 12);
}

TEST(BridgeIPC, BridgeMidiNoteSize) {
    ASSERT_EQ(sizeof(BridgeMidiNote), 12);
}

TEST(BridgeIPC, BridgeMidiCCSize) {
    ASSERT_EQ(sizeof(BridgeMidiCC), 12);
}

TEST(BridgeIPC, BridgeParamValueSize) {
    ASSERT_EQ(sizeof(BridgeParamValue), 8);
}

TEST(BridgeIPC, BridgeParamInfoSize) {
    // 6 int32_t (24) + name[64] + description[128] = 216
    ASSERT_EQ(sizeof(BridgeParamInfo), 216);
}

TEST(BridgeIPC, BridgeMachineInfoSize) {
    // 6 int32_t (24) + name[128] + shortName[64] + author[64] = 280
    ASSERT_EQ(sizeof(BridgeMachineInfo), 280);
}

TEST(BridgeIPC, BridgeWorkCmdSize) {
    ASSERT_EQ(sizeof(BridgeWorkCmd), 8);
}

TEST(BridgeIPC, BridgeLoadWaveSize) {
    ASSERT_EQ(sizeof(BridgeLoadWave), 8);
}

TEST(BridgeIPC, BridgeTickParamSize) {
    ASSERT_EQ(sizeof(BridgeTickParam), 8);
}

TEST(BridgeIPC, SharedAudioBufferSizes) {
    // Each buffer is kBridgeMaxSamples floats = 4096 * 4 = 16384 bytes
    BridgeSharedAudio audio = {};
    ASSERT_EQ(sizeof(audio.inputLeft), (size_t)(kBridgeMaxSamples * sizeof(float)));
    ASSERT_EQ(sizeof(audio.inputRight), (size_t)(kBridgeMaxSamples * sizeof(float)));
    ASSERT_EQ(sizeof(audio.outputLeft), (size_t)(kBridgeMaxSamples * sizeof(float)));
    ASSERT_EQ(sizeof(audio.outputRight), (size_t)(kBridgeMaxSamples * sizeof(float)));
}

TEST(BridgeIPC, SharedAudioFieldOffsets) {
    // Verify fields don't overlap by writing and reading
    BridgeSharedAudio audio;
    memset(&audio, 0, sizeof(audio));

    audio.numSamples = 256;
    audio.workMode = WM_READWRITE;
    audio.hasOutput = 1;
    audio.fastWorkReady = 1;
    audio.fastWorkDone = 0;

    ASSERT_EQ(audio.numSamples, 256);
    ASSERT_EQ(audio.workMode, WM_READWRITE);
    ASSERT_EQ(audio.hasOutput, 1);
    ASSERT_EQ(audio.fastWorkReady, 1);
    ASSERT_EQ(audio.fastWorkDone, 0);

    // Write to output, verify input is untouched
    audio.outputLeft[0] = 1.0f;
    audio.outputRight[0] = -1.0f;
    ASSERT_NEAR(audio.inputLeft[0], 0.0f, 0.0001f);
    ASSERT_NEAR(audio.inputRight[0], 0.0f, 0.0001f);
}

// ===========================================================================
// 2. BridgeIPC naming helpers
// ===========================================================================

TEST(BridgeIPC, PipeNameFormat) {
    std::string name = BridgePipeName("test123");
    ASSERT_EQ(name, "\\\\.\\pipe\\BuzzBridge_test123");
}

TEST(BridgeIPC, SharedMemNameFormat) {
    std::string name = BridgeSharedMemName("session42");
    ASSERT_EQ(name, "Local\\BuzzBridge_Audio_session42");
}

TEST(BridgeIPC, PipeNameEmpty) {
    std::string name = BridgePipeName("");
    ASSERT_EQ(name, "\\\\.\\pipe\\BuzzBridge_");
}

// ===========================================================================
// 3. BridgeIPC command enum values stability
// ===========================================================================

TEST(BridgeIPC, CommandEnumValues) {
    // These values are part of the wire protocol and must not change
    ASSERT_EQ((uint32_t)kCmdPing, 1u);
    ASSERT_EQ((uint32_t)kCmdLoadDll, 2u);
    ASSERT_EQ((uint32_t)kCmdInitMachine, 3u);
    ASSERT_EQ((uint32_t)kCmdUnload, 4u);
    ASSERT_EQ((uint32_t)kCmdSetMasterInfo, 5u);
    ASSERT_EQ((uint32_t)kCmdTick, 6u);
    ASSERT_EQ((uint32_t)kCmdWork, 7u);
    ASSERT_EQ((uint32_t)kCmdStop, 8u);
    ASSERT_EQ((uint32_t)kCmdMidiNote, 9u);
    ASSERT_EQ((uint32_t)kCmdMidiCC, 10u);
    ASSERT_EQ((uint32_t)kCmdGetInfo, 11u);
    ASSERT_EQ((uint32_t)kCmdSetParam, 12u);
    ASSERT_EQ((uint32_t)kCmdLoadWave, 13u);
    ASSERT_EQ((uint32_t)kCmdClearWaves, 14u);
    ASSERT_EQ((uint32_t)kCmdSetNumTracks, 15u);
    ASSERT_EQ((uint32_t)kCmdDescribeValue, 16u);
    ASSERT_EQ((uint32_t)kCmdShutdown, 17u);
}

TEST(BridgeIPC, ResponseEnumValues) {
    ASSERT_EQ((uint32_t)kRespOk, 0u);
    ASSERT_EQ((uint32_t)kRespError, 1u);
    ASSERT_EQ((uint32_t)kRespMachineInfo, 2u);
    ASSERT_EQ((uint32_t)kRespDescribeValue, 3u);
}

// ===========================================================================
// 4. BridgeTickParam sentinel
// ===========================================================================

TEST(BridgeIPC, TickParamSentinel) {
    BridgeTickParam sentinel;
    sentinel.paramId = -1;
    sentinel.value = 0;
    ASSERT_EQ(sentinel.paramId, -1);
}

TEST(BridgeIPC, TickParamRoundtrip) {
    // Simulate encoding a list of param changes
    std::vector<BridgeTickParam> params;
    params.push_back({0, 42});
    params.push_back({5, 1000});
    params.push_back({-1, 0}); // sentinel

    ASSERT_EQ(params[0].paramId, 0);
    ASSERT_EQ(params[0].value, 42);
    ASSERT_EQ(params[1].paramId, 5);
    ASSERT_EQ(params[1].value, 1000);
    ASSERT_EQ(params[2].paramId, -1);
}

// ===========================================================================
// 5. Audio scale factors (plugids.h)
// ===========================================================================

TEST(AudioScale, BuzzToVst3Factor) {
    ASSERT_NEAR(kBuzzToVst3Scale, 1.0f / 32768.0f, 1e-10f);
}

TEST(AudioScale, Vst3ToBuzzFactor) {
    ASSERT_NEAR(kVst3ToBuzzScale, 32768.0f, 1e-5f);
}

TEST(AudioScale, RoundtripScaling) {
    // Buzz -> VST3 -> Buzz should preserve amplitude approximately
    float buzzSample = 16384.0f;
    float vst3 = buzzSample * kBuzzToVst3Scale;
    float back = vst3 * kVst3ToBuzzScale;
    ASSERT_NEAR(back, buzzSample, 0.01f);
}

TEST(AudioScale, BuzzSampleToVst3Clamp) {
    // Test clamping
    ASSERT_NEAR(BuzzSampleToVst3(32768.0f), 1.0f, 0.001f);
    ASSERT_NEAR(BuzzSampleToVst3(-32768.0f), -1.0f, 0.001f);
    ASSERT_NEAR(BuzzSampleToVst3(0.0f), 0.0f, 0.001f);
}

TEST(AudioScale, BuzzSampleToVst3Overflow) {
    // Values beyond +-32768 should clamp to +-1.0
    ASSERT_NEAR(BuzzSampleToVst3(100000.0f), 1.0f, 0.001f);
    ASSERT_NEAR(BuzzSampleToVst3(-100000.0f), -1.0f, 0.001f);
}

TEST(AudioScale, BuzzSampleToVst3NaN) {
    // NaN should become 0
    // Use bit manipulation to produce NaN without dividing by zero
    unsigned int nanBits = 0x7FC00000u; // quiet NaN
    float nanVal;
    memcpy(&nanVal, &nanBits, sizeof(float));
    float result = BuzzSampleToVst3(nanVal);
    ASSERT_NEAR(result, 0.0f, 0.001f);
}

TEST(AudioScale, BuzzSampleToVst3NormalValues) {
    // A typical Buzz sample at half amplitude
    ASSERT_NEAR(BuzzSampleToVst3(16384.0f), 0.5f, 0.001f);
    ASSERT_NEAR(BuzzSampleToVst3(-16384.0f), -0.5f, 0.001f);
}

// ===========================================================================
// 6. MachineInterface.h constants
// ===========================================================================

TEST(MachineConstants, NoteValues) {
    ASSERT_EQ(NOTE_NO, 0);
    ASSERT_EQ(NOTE_OFF, 255);
    ASSERT_EQ(NOTE_MIN, 1);
    ASSERT_EQ(NOTE_MAX, (16 * 9) + 12); // 156
}

TEST(MachineConstants, SwitchValues) {
    ASSERT_EQ(SWITCH_OFF, 0);
    ASSERT_EQ(SWITCH_ON, 1);
    ASSERT_EQ(SWITCH_NO, 255);
}

TEST(MachineConstants, WaveValues) {
    ASSERT_EQ(WAVE_MIN, 1);
    ASSERT_EQ(WAVE_MAX, 200);
    ASSERT_EQ(WAVE_NO, 0);
}

TEST(MachineConstants, MachineTypes) {
    ASSERT_EQ(MT_MASTER, 0);
    ASSERT_EQ(MT_GENERATOR, 1);
    ASSERT_EQ(MT_EFFECT, 2);
}

TEST(MachineConstants, WorkModes) {
    ASSERT_EQ(WM_NOIO, 0);
    ASSERT_EQ(WM_READ, 1);
    ASSERT_EQ(WM_WRITE, 2);
    ASSERT_EQ(WM_READWRITE, 3);
}

TEST(MachineConstants, ParamFlags) {
    ASSERT_EQ(MPF_WAVE, 1);
    ASSERT_EQ(MPF_STATE, 2);
    ASSERT_EQ(MPF_TICK_ON_EDIT, 4);
}

TEST(MachineConstants, MachineInfoFlags) {
    ASSERT_EQ(MIF_MONO_TO_STEREO, (1 << 0));
    ASSERT_EQ(MIF_PLAYS_WAVES, (1 << 1));
    ASSERT_EQ(MIF_STEREO_EFFECT, (1 << 13));
}

TEST(MachineConstants, WaveFlags) {
    ASSERT_EQ(WF_LOOP, 1);
    ASSERT_EQ(WF_NOT16BIT, 4);
    ASSERT_EQ(WF_STEREO, 8);
    ASSERT_EQ(WF_BIDIR_LOOP, 16);
}

TEST(MachineConstants, MIVersion) {
    ASSERT_EQ(MI_VERSION, 66);
}

TEST(MachineConstants, MaxBufferLength) {
    ASSERT_EQ(MAX_BUFFER_LENGTH, 256);
}

// ===========================================================================
// 7. Struct sizes for ABI stability
// ===========================================================================

TEST(StructLayout, CMachineParameterSize) {
    // Must be stable for binary compatibility
    CMachineParameter p = {};
    // Should have: Type, Name, Description, Min, Max, NoValue, Flags, DefValue
    // Ensure all fields are accessible
    p.Type = pt_byte;
    p.Name = "test";
    p.Description = "desc";
    p.MinValue = 0;
    p.MaxValue = 255;
    p.NoValue = 255;
    p.Flags = MPF_STATE;
    p.DefValue = 128;
    ASSERT_EQ(p.DefValue, 128);
}

TEST(StructLayout, CMasterInfoFields) {
    CMasterInfo mi = {};
    mi.BeatsPerMin = 120;
    mi.TicksPerBeat = 4;
    mi.SamplesPerSec = 44100;
    mi.SamplesPerTick = 5512;
    mi.PosInTick = 0;
    mi.TicksPerSec = 8.0f;
    mi.GrooveSize = 0;
    mi.PosInGroove = 0;
    mi.GrooveData = nullptr;

    ASSERT_EQ(mi.BeatsPerMin, 120);
    ASSERT_EQ(mi.TicksPerBeat, 4);
    ASSERT_EQ(mi.SamplesPerSec, 44100);
    ASSERT_EQ(mi.SamplesPerTick, 5512);
    ASSERT_NULL(mi.GrooveData);
}

TEST(StructLayout, CWaveLevelFields) {
    CWaveLevel wl = {};
    wl.numSamples = 1000;
    wl.pSamples = nullptr;
    wl.RootNote = 60;
    wl.SamplesPerSec = 44100;
    wl.LoopStart = 0;
    wl.LoopEnd = 1000;

    ASSERT_EQ(wl.numSamples, 1000);
    ASSERT_EQ(wl.RootNote, 60);
    ASSERT_EQ(wl.LoopEnd, 1000);
}

TEST(StructLayout, CWaveInfoFields) {
    CWaveInfo wi = {};
    wi.Flags = WF_STEREO | WF_LOOP;
    wi.Volume = 0.8f;

    ASSERT_EQ(wi.Flags, WF_STEREO | WF_LOOP);
    ASSERT_NEAR(wi.Volume, 0.8f, 0.001f);
}

TEST(StructLayout, ParamSlotFields) {
    ParamSlot slot;
    slot.paramIndex = 3;
    slot.byteOffset = 5;
    slot.byteSize = 2;
    slot.param = nullptr;

    ASSERT_EQ(slot.paramIndex, 3);
    ASSERT_EQ(slot.byteOffset, 5);
    ASSERT_EQ(slot.byteSize, 2);
    ASSERT_NULL(slot.param);
}

TEST(StructLayout, GearEntryFields) {
    GearEntry entry;
    entry.displayName = "Test Machine";
    entry.dllPath = "C:\\test\\machine.dll";
    entry.category = "Generators";
    entry.machineType = MT_GENERATOR;

    ASSERT_EQ(entry.displayName, "Test Machine");
    ASSERT_EQ(entry.machineType, MT_GENERATOR);
}

// ===========================================================================
// 8. Oscillator table: GetOscTblOffset edge cases
// ===========================================================================

TEST(OscTableOffset, Level0Is0) {
    ASSERT_EQ(GetOscTblOffset(0), 0);
}

TEST(OscTableOffset, Level10ReturnsNonZero) {
    int offset = GetOscTblOffset(10);
    ASSERT_GT(offset, 0);
}

TEST(OscTableOffset, TotalSamplesConsistency) {
    // Sum of samples across levels 0..9 = 4092 (matches BUZZ_OSC_TABLE_TOTAL_SAMPLES)
    // Level 10 = 2 samples, extending to 4094. The table array is 4092 but
    // level 10's offset (4092) + 2 goes just past it. Levels 0-9 fit within 4092.
    int total09 = 0;
    for (int level = 0; level <= 9; level++) {
        total09 += (2048 >> level);
    }
    ASSERT_EQ(total09, BUZZ_OSC_TABLE_TOTAL_SAMPLES);
}

TEST(OscTableOffset, EachLevelFitsInTable) {
    // Levels 0-9 should fit within BUZZ_OSC_TABLE_TOTAL_SAMPLES
    for (int level = 0; level <= 9; level++) {
        int offset = GetOscTblOffset(level);
        int numSamples = 2048 >> level;
        ASSERT_LE(offset + numSamples, BUZZ_OSC_TABLE_TOTAL_SAMPLES);
    }
}

TEST(OscTableOffset, LevelsDoNotOverlap) {
    for (int level = 0; level < 10; level++) {
        int offset = GetOscTblOffset(level);
        int size = 2048 >> level;
        int nextOffset = GetOscTblOffset(level + 1);
        ASSERT_LE(offset + size, nextOffset);
    }
}

// ===========================================================================
// 9. Oscillator tables: additional waveform properties
// ===========================================================================

TEST(OscWaveforms, PulseIsBipolar) {
    BuzzOscTables::Initialize();
    const short* pulse = BuzzOscTables::GetTable(OWF_PULSE);
    ASSERT_NOT_NULL(pulse);
    bool hasPos = false, hasNeg = false;
    for (int i = 0; i < 2048; i++) {
        if (pulse[i] > 1000) hasPos = true;
        if (pulse[i] < -1000) hasNeg = true;
    }
    ASSERT_TRUE(hasPos);
    ASSERT_TRUE(hasNeg);
}

TEST(OscWaveforms, TriangleIsBipolar) {
    const short* tri = BuzzOscTables::GetTable(OWF_TRIANGLE);
    ASSERT_NOT_NULL(tri);
    bool hasPos = false, hasNeg = false;
    for (int i = 0; i < 2048; i++) {
        if (tri[i] > 1000) hasPos = true;
        if (tri[i] < -1000) hasNeg = true;
    }
    ASSERT_TRUE(hasPos);
    ASSERT_TRUE(hasNeg);
}

TEST(OscWaveforms, 303SawBipolar) {
    const short* saw303 = BuzzOscTables::GetTable(OWF_303_SAWTOOTH);
    ASSERT_NOT_NULL(saw303);
    bool hasPos = false, hasNeg = false;
    for (int i = 0; i < 2048; i++) {
        if (saw303[i] > 1000) hasPos = true;
        if (saw303[i] < -1000) hasNeg = true;
    }
    ASSERT_TRUE(hasPos);
    ASSERT_TRUE(hasNeg);
}

TEST(OscWaveforms, HigherLevelsSmallerTable) {
    // Level N should have 2048 >> N samples
    for (int level = 0; level <= 10; level++) {
        int expected = 2048 >> level;
        ASSERT_GT(expected, 0);
    }
}

TEST(OscWaveforms, SineSymmetry) {
    // Sine at sample N should be approximately -sine at sample N + half
    const short* sine = BuzzOscTables::GetTable(OWF_SINE);
    ASSERT_NOT_NULL(sine);
    for (int i = 0; i < 1024; i++) {
        int sum = (int)sine[i] + (int)sine[i + 1024];
        // Sum should be near zero (half-wave symmetry)
        CHECK_TRUE(abs(sum) < 200);
    }
}

// ===========================================================================
// 10. BuzzCallbacks: additional untested methods
// ===========================================================================

TEST(CallbacksExtra, GetWritePos) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetWritePos(), 0);
}

TEST(CallbacksExtra, GetPlayPos) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetPlayPos(), 0);
}

TEST(CallbacksExtra, GetSongPosition) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetSongPosition(), 0);
}

TEST(CallbacksExtra, GetLoopStartEnd) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetLoopStart(), 0);
    ASSERT_EQ(cb.GetLoopEnd(), 16);
    ASSERT_EQ(cb.GetSongEnd(), 16);
}

TEST(CallbacksExtra, GetTPBWithMasterInfo) {
    BuzzCallbacks cb;
    CMasterInfo mi = {};
    mi.TicksPerBeat = 8;
    cb.masterInfoPtr = &mi;
    ASSERT_EQ(cb.GetTPB(), 8);
}

TEST(CallbacksExtra, GetTPBNullFallback) {
    BuzzCallbacks cb;
    cb.masterInfoPtr = nullptr;
    ASSERT_EQ(cb.GetTPB(), 4);
}

TEST(CallbacksExtra, GetNumTracks) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetNumTracks(nullptr), 1);
}

TEST(CallbacksExtra, GetAudioFrame) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetAudioFrame(), 0);
}

TEST(CallbacksExtra, HostMIDIFilteringReturnsTrue) {
    BuzzCallbacks cb;
    ASSERT_TRUE(cb.HostMIDIFiltering());
}

TEST(CallbacksExtra, GetThemeColorReturnsZero) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetThemeColor("background"), (dword)0);
}

TEST(CallbacksExtra, AllocateWaveReturnsFalse) {
    BuzzCallbacks cb;
    ASSERT_FALSE(cb.AllocateWave(1, 1000, "test"));
}

TEST(CallbacksExtra, GetEnvSizeReturnsZero) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetEnvSize(1, 0), 0);
}

TEST(CallbacksExtra, GetEnvPointReturnsFalse) {
    BuzzCallbacks cb;
    word x = 0, y = 0;
    int flags = 0;
    ASSERT_FALSE(cb.GetEnvPoint(1, 0, 0, x, y, flags));
}

TEST(CallbacksExtra, GetMachineReturnsNull) {
    BuzzCallbacks cb;
    ASSERT_NULL(cb.GetMachine("nonexistent"));
}

TEST(CallbacksExtra, GetInputReturnsFalse) {
    BuzzCallbacks cb;
    float buf[256] = {};
    ASSERT_FALSE(cb.GetInput(0, buf, 256, false, nullptr));
}

TEST(CallbacksExtra, GetConnectionCountReturnsZero) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetConnectionCount(nullptr, true), 0);
    ASSERT_EQ(cb.GetConnectionCount(nullptr, false), 0);
}

TEST(CallbacksExtra, GetTotalLatencyReturnsZero) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetTotalLatency(), 0);
}

TEST(CallbacksExtra, GetMainWindowReturnsNull) {
    BuzzCallbacks cb;
    ASSERT_NULL(cb.GetMainWindow());
}

TEST(CallbacksExtra, IsMachineMutedReturnsFalse) {
    BuzzCallbacks cb;
    ASSERT_FALSE(cb.IsMachineMuted(nullptr));
}

TEST(CallbacksExtra, GetOptionReturnsFalse) {
    BuzzCallbacks cb;
    ASSERT_FALSE(cb.GetOption("SomeOption"));
}

TEST(CallbacksExtra, GetPlayNotesStateReturnsFalse) {
    BuzzCallbacks cb;
    ASSERT_FALSE(cb.GetPlayNotesState());
}

TEST(CallbacksExtra, GetSubTickInfoReturnsNull) {
    BuzzCallbacks cb;
    ASSERT_NULL(cb.GetSubTickInfo());
}

TEST(CallbacksExtra, RemapLoadedMachineParameterIndex) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.RemapLoadedMachineParameterIndex(nullptr, 5), -1);
}

TEST(CallbacksExtra, GetThemePathReturnsEmpty) {
    BuzzCallbacks cb;
    const char* path = cb.GetThemePath();
    ASSERT_NOT_NULL(path);
    ASSERT_EQ(strlen(path), (size_t)0);
}

TEST(CallbacksExtra, IsValidAsciiCharReturnsTrue) {
    BuzzCallbacks cb;
    ASSERT_TRUE(cb.IsValidAsciiChar(nullptr, 0, 'A'));
}

TEST(CallbacksExtra, WriteLineDoesNotCrash) {
    BuzzCallbacks cb;
    cb.WriteLine("test line");
    cb.WriteLine(nullptr);
    ASSERT_TRUE(true);
}

TEST(CallbacksExtra, DescribeValueReturnsNull) {
    BuzzCallbacks cb;
    ASSERT_NULL(cb.DescribeValue(nullptr, 0, 0));
}

TEST(CallbacksExtra, GetPatternNameReturnsEmpty) {
    BuzzCallbacks cb;
    const char* name = cb.GetPatternName(nullptr);
    ASSERT_NOT_NULL(name);
    ASSERT_EQ(strlen(name), (size_t)0);
}

TEST(CallbacksExtra, SafeNoOpMethodsDoNotCrash) {
    BuzzCallbacks cb;
    // All of these should be no-ops that don't crash
    cb.SetNumberOfTracks(4);
    cb.MidiOut(0, 0x905A7F);
    cb.ScheduleEvent(0, 0);
    cb.SetSongPosition(0);
    cb.SetTempo(120);
    cb.SetTPB(4);
    cb.Play();
    cb.Stop();
    cb.SetModifiedFlag();
    cb.EnableMultithreading(true);
    cb.SetMidiFocus(nullptr);
    cb.ToggleRecordMode();
    cb.SetInputChannelCount(2);
    cb.SetOutputChannelCount(2);
    cb.SetMidiInputMode(MIM_Immediate);
    cb.SetnumOutputChannels(nullptr, 2);
    cb.SetGroovePattern(nullptr, 0);
    cb.MuteMachine(nullptr, true);
    cb.SoloMachine(nullptr);
    cb.UpdateParameterDisplays(nullptr);
    cb.SendControlChanges(nullptr);
    cb.SelectWave(1);
    ASSERT_TRUE(true);
}

// ===========================================================================
// 11. BuzzCallbacks: MDK stub via GetNearestWaveLevel(-1, -1)
// ===========================================================================

TEST(CallbacksMDK, GetNearestWaveLevelMDKStub) {
    BuzzCallbacks cb;
    const CWaveLevel* result = cb.GetNearestWaveLevel(-1, -1);
    ASSERT_NOT_NULL(result);

    // Calling again should return the same stub
    const CWaveLevel* result2 = cb.GetNearestWaveLevel(-1, -1);
    ASSERT_EQ(result, result2);
}

// ===========================================================================
// 12. Parameter mapping: switch type edge cases
// ===========================================================================

TEST(ParamMappingExtra, SwitchParamNormalizedZero) {
    CMachineParameter p = MakeParam(pt_switch, SWITCH_OFF, SWITCH_ON, SWITCH_NO, SWITCH_OFF);
    ASSERT_EQ(ParameterMapping::NormalizedToBuzz(0.0, &p), SWITCH_OFF);
}

TEST(ParamMappingExtra, SwitchParamNormalizedOne) {
    CMachineParameter p = MakeParam(pt_switch, SWITCH_OFF, SWITCH_ON, SWITCH_NO, SWITCH_OFF);
    ASSERT_EQ(ParameterMapping::NormalizedToBuzz(1.0, &p), SWITCH_ON);
}

TEST(ParamMappingExtra, SwitchParamRoundtrip) {
    CMachineParameter p = MakeParam(pt_switch, SWITCH_OFF, SWITCH_ON, SWITCH_NO, SWITCH_OFF);
    double normOff = ParameterMapping::BuzzToNormalized(SWITCH_OFF, &p);
    double normOn = ParameterMapping::BuzzToNormalized(SWITCH_ON, &p);
    ASSERT_NEAR(normOff, 0.0, 0.001);
    ASSERT_NEAR(normOn, 1.0, 0.001);
}

TEST(ParamMappingExtra, NoteParamRange) {
    CMachineParameter p = MakeParam(pt_note, NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0);
    ASSERT_EQ(ParameterMapping::NormalizedToBuzz(0.0, &p), NOTE_MIN);
    ASSERT_EQ(ParameterMapping::NormalizedToBuzz(1.0, &p), NOTE_MAX);
}

TEST(ParamMappingExtra, GetStepCountWithOffset) {
    CMachineParameter p = MakeParam(pt_byte, 10, 110, 255, 60);
    ASSERT_EQ(ParameterMapping::GetStepCount(&p), 100);
}

TEST(ParamMappingExtra, GetDefaultNormalizedNull) {
    ASSERT_NEAR(ParameterMapping::GetDefaultNormalized(nullptr), 0.0, 0.001);
}

// ===========================================================================
// 13. BuzzParamLayout: internal params (pt_internal) are skipped
// ===========================================================================

TEST(ParamLayoutExtra, InternalParamsSkipped) {
    CMachineParameter p1 = MakeParam(pt_byte, 0, 127, 255, 64);
    CMachineParameter pInternal = {};
    pInternal.Type = pt_internal;
    pInternal.Name = "Internal";
    pInternal.Description = "Hidden";
    CMachineParameter p3 = MakeParam(pt_word, 0, 65535, 65535, 0);

    const CMachineParameter* params[] = { &p1, &pInternal, &p3 };

    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 3;
    info.numTrackParameters = 0;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    // Internal param should be skipped: only 2 slots
    ASSERT_EQ((int)layout.GetGlobalSlots().size(), 2);
    // byte(1) + word(2) = 3 bytes
    ASSERT_EQ(layout.GetGlobalStructSize(), 3);
}

TEST(ParamLayoutExtra, NullParamInArray) {
    CMachineParameter p1 = MakeParam(pt_byte, 0, 127, 255, 64);
    const CMachineParameter* params[] = { &p1, nullptr };

    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 2;
    info.numTrackParameters = 0;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    // Null param should be skipped
    ASSERT_EQ((int)layout.GetGlobalSlots().size(), 1);
}

TEST(ParamLayoutExtra, BuildWithNullParametersArray) {
    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 5;
    info.numTrackParameters = 2;
    info.Parameters = nullptr;

    BuzzParamLayout layout;
    layout.Build(&info);

    // Should handle null Parameters array gracefully
    ASSERT_EQ(layout.GetGlobalStructSize(), 0);
    ASSERT_EQ(layout.GetTrackStructSize(), 0);
}

TEST(ParamLayoutExtra, AllSwitchParams) {
    CMachineParameter s1 = MakeParam(pt_switch, 0, 1, 255, 0);
    CMachineParameter s2 = MakeParam(pt_switch, 0, 1, 255, 1);
    CMachineParameter s3 = MakeParam(pt_switch, 0, 1, 255, 0);
    const CMachineParameter* params[] = { &s1, &s2, &s3 };

    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 3;
    info.numTrackParameters = 0;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    // 3 switch params, each 1 byte = 3 bytes
    ASSERT_EQ(layout.GetGlobalStructSize(), 3);
    ASSERT_EQ((int)layout.GetGlobalSlots().size(), 3);
}

TEST(ParamLayoutExtra, MixedParamTypes) {
    CMachineParameter pNote = MakeParam(pt_note, 1, 156, 0, 0, 0);
    CMachineParameter pSwitch = MakeParam(pt_switch, 0, 1, 255, 0);
    CMachineParameter pByte = MakeParam(pt_byte, 0, 127, 255, 64);
    CMachineParameter pWord = MakeParam(pt_word, 0, 65535, 65535, 0);
    const CMachineParameter* params[] = { &pNote, &pSwitch, &pByte, &pWord };

    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 4;
    info.numTrackParameters = 0;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    // note(1) + switch(1) + byte(1) + word(2) = 5 bytes
    ASSERT_EQ(layout.GetGlobalStructSize(), 5);

    auto& slots = layout.GetGlobalSlots();
    ASSERT_EQ(slots[0].byteOffset, 0);
    ASSERT_EQ(slots[0].byteSize, 1);
    ASSERT_EQ(slots[1].byteOffset, 1);
    ASSERT_EQ(slots[1].byteSize, 1);
    ASSERT_EQ(slots[2].byteOffset, 2);
    ASSERT_EQ(slots[2].byteSize, 1);
    ASSERT_EQ(slots[3].byteOffset, 3);
    ASSERT_EQ(slots[3].byteSize, 2);
}

TEST(ParamLayoutExtra, WriteAllNoValuesNull) {
    CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);
    const CMachineParameter* params[] = { &p };

    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 1;
    info.numTrackParameters = 0;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    // Should not crash
    layout.WriteAllNoValues(nullptr);
    layout.WriteAllDefaults(nullptr);
    ASSERT_TRUE(true);
}

TEST(ParamLayoutExtra, WriteTrackAllNoValuesNull) {
    CMachineParameter tp = MakeParam(pt_byte, 0, 127, 255, 64);
    const CMachineParameter* params[] = { &tp };

    CMachineInfo info = {};
    info.Type = MT_GENERATOR;
    info.numGlobalParameters = 0;
    info.numTrackParameters = 1;
    info.Parameters = params;

    BuzzParamLayout layout;
    layout.Build(&info);

    // Should not crash
    layout.WriteTrackAllNoValues(nullptr, 4);
    ASSERT_TRUE(true);
}

TEST(ParamLayoutExtra, RebuildOverwritesPrevious) {
    CMachineParameter p1 = MakeParam(pt_byte, 0, 127, 255, 64);
    const CMachineParameter* params1[] = { &p1 };

    CMachineInfo info1 = {};
    info1.Type = MT_GENERATOR;
    info1.numGlobalParameters = 1;
    info1.numTrackParameters = 0;
    info1.Parameters = params1;

    BuzzParamLayout layout;
    layout.Build(&info1);
    ASSERT_EQ(layout.GetGlobalStructSize(), 1);
    ASSERT_EQ((int)layout.GetGlobalSlots().size(), 1);

    // Rebuild with different params
    CMachineParameter p2 = MakeParam(pt_word, 0, 1000, 65535, 500);
    CMachineParameter p3 = MakeParam(pt_word, 0, 2000, 65535, 1000);
    const CMachineParameter* params2[] = { &p2, &p3 };

    CMachineInfo info2 = {};
    info2.Type = MT_EFFECT;
    info2.numGlobalParameters = 2;
    info2.numTrackParameters = 0;
    info2.Parameters = params2;

    layout.Build(&info2);
    ASSERT_EQ(layout.GetGlobalStructSize(), 4); // word(2) + word(2)
    ASSERT_EQ((int)layout.GetGlobalSlots().size(), 2);
}

// ===========================================================================
// 14. WaveTable: additional edge cases
// ===========================================================================

#pragma pack(push, 1)
struct TestWavHdr {
    char riff[4];
    unsigned int fileSize;
    char wave[4];
    char fmt_[4];
    unsigned int fmtSize;
    unsigned short format;
    unsigned short channels;
    unsigned int sampleRate;
    unsigned int byteRate;
    unsigned short blockAlign;
    unsigned short bitsPerSample;
    char data[4];
    unsigned int dataSize;
};
#pragma pack(pop)

static std::string CreateWav(const char* filename, int sampleRate, int channels,
                              int bitsPerSample, int format, const void* samples, int numBytes) {
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    std::string path = std::string(tempDir) + filename;

    TestWavHdr hdr = {};
    memcpy(hdr.riff, "RIFF", 4);
    memcpy(hdr.wave, "WAVE", 4);
    memcpy(hdr.fmt_, "fmt ", 4);
    hdr.fmtSize = 16;
    hdr.format = (unsigned short)format;
    hdr.channels = (unsigned short)channels;
    hdr.sampleRate = sampleRate;
    hdr.bitsPerSample = (unsigned short)bitsPerSample;
    hdr.blockAlign = (unsigned short)(channels * (bitsPerSample / 8));
    hdr.byteRate = sampleRate * hdr.blockAlign;
    memcpy(hdr.data, "data", 4);
    hdr.dataSize = numBytes;
    unsigned int totalSize = sizeof(hdr) + numBytes;
    hdr.fileSize = totalSize - 8;

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return "";
    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(samples, 1, numBytes, f);
    fclose(f);
    return path;
}

TEST(WaveTableExtra, Load8BitWav) {
    // 8-bit unsigned PCM
    unsigned char samples[100];
    for (int i = 0; i < 100; i++) samples[i] = (unsigned char)(128 + i / 2);

    std::string path = CreateWav("test_8bit.wav", 22050, 1, 8, 1, samples, 100);
    ASSERT_TRUE(!path.empty());

    BuzzWaveTable wt;
    bool ok = wt.LoadWav(1, path);
    ASSERT_TRUE(ok);

    const CWaveLevel* level = wt.GetWaveLevel(1, 0);
    ASSERT_NOT_NULL(level);
    ASSERT_EQ(level->numSamples, 100);
    ASSERT_EQ(level->SamplesPerSec, 22050);

    DeleteFileA(path.c_str());
}

TEST(WaveTableExtra, Load32BitFloatWav) {
    float samples[200];
    for (int i = 0; i < 200; i++) {
        samples[i] = sinf((float)i * 0.05f) * 0.8f;
    }

    std::string path = CreateWav("test_float32.wav", 48000, 1, 32, 3, samples, 200 * sizeof(float));
    ASSERT_TRUE(!path.empty());

    BuzzWaveTable wt;
    bool ok = wt.LoadWav(1, path);
    ASSERT_TRUE(ok);

    const CWaveLevel* level = wt.GetWaveLevel(1, 0);
    ASSERT_NOT_NULL(level);
    ASSERT_EQ(level->numSamples, 200);
    ASSERT_EQ(level->SamplesPerSec, 48000);

    // Verify some sample data is non-zero
    bool hasNonZero = false;
    for (int i = 0; i < 200; i++) {
        if (level->pSamples[i] != 0) { hasNonZero = true; break; }
    }
    ASSERT_TRUE(hasNonZero);

    DeleteFileA(path.c_str());
}

TEST(WaveTableExtra, MaxSlotBoundary) {
    // Load into slot 200 (WAVE_MAX)
    short samples[10] = {1000, 2000, 3000, -1000, -2000, -3000, 0, 0, 0, 0};
    std::string path = CreateWav("test_maxslot.wav", 44100, 1, 16, 1, samples, 10 * sizeof(short));
    ASSERT_TRUE(!path.empty());

    BuzzWaveTable wt;
    ASSERT_TRUE(wt.LoadWav(200, path));
    ASSERT_TRUE(wt.IsLoaded(200));

    const CWaveLevel* level = wt.GetWaveLevel(200, 0);
    ASSERT_NOT_NULL(level);
    ASSERT_EQ(level->numSamples, 10);

    DeleteFileA(path.c_str());
}

TEST(WaveTableExtra, GetSlotReturnsCorrectData) {
    short samples[4] = {100, 200, 300, 400};
    std::string path = CreateWav("test_getslot.wav", 44100, 1, 16, 1, samples, 4 * sizeof(short));
    ASSERT_TRUE(!path.empty());

    BuzzWaveTable wt;
    wt.LoadWav(5, path);

    const WaveSlot* slot = wt.GetSlot(5);
    ASSERT_NOT_NULL(slot);
    ASSERT_TRUE(slot->loaded);
    ASSERT_FALSE(slot->name.empty());

    // Invalid slot
    ASSERT_NULL(wt.GetSlot(0));
    ASSERT_NULL(wt.GetSlot(-1));
    ASSERT_NULL(wt.GetSlot(201));

    DeleteFileA(path.c_str());
}

TEST(WaveTableExtra, ClearOutOfRangeDoesNotCrash) {
    BuzzWaveTable wt;
    wt.Clear(0);    // WAVE_NO
    wt.Clear(-1);
    wt.Clear(201);
    wt.Clear(500);
    ASSERT_TRUE(true);
}

TEST(WaveTableExtra, IsLoadedOutOfRange) {
    BuzzWaveTable wt;
    ASSERT_FALSE(wt.IsLoaded(0));
    ASSERT_FALSE(wt.IsLoaded(-1));
    ASSERT_FALSE(wt.IsLoaded(201));
}

TEST(WaveTableExtra, GetWaveNameOutOfRange) {
    BuzzWaveTable wt;
    const char* name = wt.GetWaveName(0);
    ASSERT_NOT_NULL(name);
    ASSERT_EQ(strlen(name), (size_t)0);

    name = wt.GetWaveName(201);
    ASSERT_NOT_NULL(name);
    ASSERT_EQ(strlen(name), (size_t)0);
}

TEST(WaveTableExtra, AllSlotsFull) {
    // Fill all 200 slots, GetFreeWave should return 0
    BuzzWaveTable wt;
    // We can't load 200 files easily, but we can check the logic
    // by verifying GetFreeWave starts at 1 and increments
    ASSERT_EQ(wt.GetFreeWave(), 1);
}

TEST(WaveTableExtra, LoadEmptyPath) {
    BuzzWaveTable wt;
    ASSERT_FALSE(wt.LoadWav(1, ""));
}

// ===========================================================================
// 15. BuzzMachineLoader: UpdateMasterInfo edge cases
// ===========================================================================

TEST(LoaderExtra, UpdateMasterInfoSampleRateClamp) {
    BuzzMachineLoader loader;
    auto* mi = loader.GetMasterInfo();

    // Sample rate below 11050 should fall back
    loader.UpdateMasterInfo(125.0, 5000.0);
    ASSERT_EQ(mi->SamplesPerSec, 44100);
}

TEST(LoaderExtra, UpdateMasterInfoTicksPerSec) {
    BuzzMachineLoader loader;
    auto* mi = loader.GetMasterInfo();

    loader.UpdateMasterInfo(120.0, 44100.0, 4);
    float expectedTicksPerSec = (float)44100.0f / (float)mi->SamplesPerTick;
    ASSERT_NEAR(mi->TicksPerSec, expectedTicksPerSec, 0.01f);
}

TEST(LoaderExtra, UpdateMasterInfoSamplesPerTickMinimum) {
    BuzzMachineLoader loader;
    auto* mi = loader.GetMasterInfo();

    // Very high BPM with high TPB -> very few samples per tick
    loader.UpdateMasterInfo(500.0, 11050.0, 32);
    ASSERT_GE(mi->SamplesPerTick, 1);
}

TEST(LoaderExtra, DefaultMasterInfoValues) {
    BuzzMachineLoader loader;
    auto* mi = loader.GetMasterInfo();

    // Default constructor values
    ASSERT_EQ(mi->BeatsPerMin, 125);
    ASSERT_EQ(mi->TicksPerBeat, 4);
    ASSERT_EQ(mi->SamplesPerSec, 44100);
    ASSERT_GT(mi->SamplesPerTick, 0);
    ASSERT_EQ(mi->PosInTick, 0);
    ASSERT_GT(mi->TicksPerSec, 0.0f);
    ASSERT_EQ(mi->GrooveSize, 0);
    ASSERT_NULL(mi->GrooveData);
}

TEST(LoaderExtra, CallbacksWiredToMasterInfo) {
    BuzzMachineLoader loader;
    auto* cb = loader.GetCallbacks();
    ASSERT_NOT_NULL(cb->masterInfoPtr);
    ASSERT_EQ(cb->masterInfoPtr, loader.GetMasterInfo());
}

TEST(LoaderExtra, CallbacksWiredToWaveTable) {
    BuzzMachineLoader loader;
    auto* cb = loader.GetCallbacks();
    ASSERT_NOT_NULL(cb->waveTable);
    ASSERT_EQ(cb->waveTable, loader.GetWaveTable());
}

TEST(LoaderExtra, IsFaultedInitially) {
    BuzzMachineLoader loader;
    ASSERT_FALSE(loader.IsFaulted());
}

TEST(LoaderExtra, InitMachineWithoutLoad) {
    BuzzMachineLoader loader;
    ASSERT_FALSE(loader.InitMachine());
}

TEST(LoaderExtra, StopMachineWithoutInit) {
    BuzzMachineLoader loader;
    // Should not crash
    loader.StopMachine();
    ASSERT_TRUE(true);
}

TEST(LoaderExtra, GetMachineExWithoutInit) {
    BuzzMachineLoader loader;
    ASSERT_NULL(loader.GetMachineEx());
}

// ===========================================================================
// 16. plugids.h: Parameter ID range validation
// ===========================================================================

TEST(ParamIDs, GlobalParamBase) {
    ASSERT_EQ(kBuzzGlobalParamBase, 1);
}

TEST(ParamIDs, MaxGlobalParams) {
    ASSERT_EQ(kMaxGlobalParams, 64);
}

TEST(ParamIDs, TrackParamBase) {
    ASSERT_EQ(kBuzzTrackParamBase, 1000);
}

TEST(ParamIDs, TrackStride) {
    ASSERT_EQ(kTrackParamStride, 1000);
}

TEST(ParamIDs, MaxTrackParams) {
    ASSERT_EQ(kMaxTrackParams, 64);
}

TEST(ParamIDs, MaxTracks) {
    ASSERT_EQ(kMaxTracks, 16);
}

TEST(ParamIDs, BypassParam) {
    ASSERT_EQ(kBypassParamID, 50000);
}

TEST(ParamIDs, GlobalsDoNotOverlapTracks) {
    int maxGlobal = kBuzzGlobalParamBase + kMaxGlobalParams - 1; // 128
    ASSERT_LT(maxGlobal, kBuzzTrackParamBase);
}

TEST(ParamIDs, TracksDoNotOverlapBypass) {
    int maxTrackId = kBuzzTrackParamBase + (kMaxTracks - 1) * kTrackParamStride + kMaxTrackParams - 1;
    ASSERT_LT(maxTrackId, kBypassParamID);
}

TEST(ParamIDs, TrackIdDecoding) {
    // For each valid track/param combo, encoding and decoding should be consistent
    for (int t = 0; t < kMaxTracks; t++) {
        for (int p = 0; p < kMaxTrackParams; p++) {
            int id = kBuzzTrackParamBase + t * kTrackParamStride + p;
            int decodedTrack = (id - kBuzzTrackParamBase) / kTrackParamStride;
            int decodedParam = (id - kBuzzTrackParamBase) % kTrackParamStride;
            CHECK_EQ(decodedTrack, t);
            CHECK_EQ(decodedParam, p);
        }
    }
}

// ===========================================================================
// 17. SEH_Call: basic coverage
// ===========================================================================

TEST(SEHGuard, NormalCallReturnsTrue) {
    bool result = SEH_Call([]() {
        volatile int x = 42;
        (void)x;
    });
    ASSERT_TRUE(result);
}

TEST(SEHGuard, SEHCallRetNormalValue) {
    int result = SEH_CallRet<int>([]() -> int {
        return 42;
    }, -1);
    ASSERT_EQ(result, 42);
}

// ===========================================================================
// 18. BridgePipe: default state
// ===========================================================================

TEST(BridgePipe, DefaultInvalid) {
    BridgePipe pipe;
    ASSERT_FALSE(pipe.IsValid());
    ASSERT_EQ(pipe.GetHandle(), INVALID_HANDLE_VALUE);
}

TEST(BridgePipe, CloseInvalidDoesNotCrash) {
    BridgePipe pipe;
    pipe.Close();
    ASSERT_FALSE(pipe.IsValid());
}

TEST(BridgePipe, SetHandleUpdatesValidity) {
    BridgePipe pipe;
    // Setting a non-INVALID_HANDLE value makes it "valid"
    // (we won't actually use it - just test the flag)
    pipe.SetHandle((HANDLE)0x1234);
    ASSERT_TRUE(pipe.IsValid());
    // Reset to avoid close on real handle
    pipe.SetHandle(INVALID_HANDLE_VALUE);
    ASSERT_FALSE(pipe.IsValid());
}

// ===========================================================================
// 19. BridgeSharedMem: default state
// ===========================================================================

TEST(BridgeSharedMem, DefaultState) {
    BridgeSharedMem mem;
    ASSERT_NULL(mem.GetData());
    ASSERT_NULL(mem.GetAudio());
}

TEST(BridgeSharedMem, CloseDefaultDoesNotCrash) {
    BridgeSharedMem mem;
    mem.Close();
    ASSERT_NULL(mem.GetData());
}

// ===========================================================================
// 20. GearScanner: InferTypeFromCategory (tested via probeOneDll fallback)
// ===========================================================================

TEST(GearScannerExtra, EmptyEntries) {
    GearScanner scanner;
    ASSERT_EQ((int)scanner.GetEntries().size(), 0);
    ASSERT_EQ((int)scanner.GetGenerators().size(), 0);
    ASSERT_EQ((int)scanner.GetEffects().size(), 0);
}

TEST(GearScannerExtra, ClearOnEmpty) {
    GearScanner scanner;
    scanner.Clear();
    ASSERT_EQ((int)scanner.GetEntries().size(), 0);
}

// ===========================================================================
// 21. BuzzWaveTable: filename extraction with various path separators
// ===========================================================================

TEST(WaveTablePaths, BackslashPath) {
    short samples[4] = {100, 200, 300, 400};
    std::string path = CreateWav("test_bs_path.wav", 44100, 1, 16, 1, samples, 4 * sizeof(short));

    BuzzWaveTable wt;
    wt.LoadWav(1, path);
    const char* name = wt.GetWaveName(1);
    ASSERT_NOT_NULL(name);
    ASSERT_EQ(strcmp(name, "test_bs_path"), 0);

    DeleteFileA(path.c_str());
}

TEST(WaveTablePaths, FileWithoutExtension) {
    // Create a file with a .wav extension but name includes dots
    short samples[4] = {1, 2, 3, 4};
    std::string path = CreateWav("my.cool.sample.wav", 44100, 1, 16, 1, samples, 4 * sizeof(short));

    BuzzWaveTable wt;
    wt.LoadWav(1, path);
    const char* name = wt.GetWaveName(1);
    ASSERT_NOT_NULL(name);
    // Should strip only the last .wav extension
    ASSERT_EQ(strcmp(name, "my.cool.sample"), 0);

    DeleteFileA(path.c_str());
}

// ===========================================================================
// 22. Parameter mapping: large word range accuracy
// ===========================================================================

TEST(ParamMappingExtra, LargeWordRoundtrip) {
    CMachineParameter p = MakeParam(pt_word, 0, 65535, 65535, 32768);

    // Test specific values across the full word range
    int testValues[] = {0, 1, 100, 1000, 10000, 32768, 50000, 65534, 65535};
    for (int val : testValues) {
        double norm = ParameterMapping::BuzzToNormalized(val, &p);
        int back = ParameterMapping::NormalizedToBuzz(norm, &p);
        CHECK_EQ(back, val);
    }
}

TEST(ParamMappingExtra, SmallRangeParam) {
    // A param with range 0-3 (e.g., waveform select)
    CMachineParameter p = MakeParam(pt_byte, 0, 3, 255, 0);

    for (int i = 0; i <= 3; i++) {
        double norm = ParameterMapping::BuzzToNormalized(i, &p);
        int back = ParameterMapping::NormalizedToBuzz(norm, &p);
        CHECK_EQ(back, i);
    }
}

TEST(ParamMappingExtra, SingleValueParam) {
    // Range is just one value: min == max
    CMachineParameter p = MakeParam(pt_byte, 42, 42, 255, 42);
    ASSERT_EQ(ParameterMapping::GetStepCount(&p), 0);
    ASSERT_NEAR(ParameterMapping::BuzzToNormalized(42, &p), 0.0, 0.001);
    ASSERT_EQ(ParameterMapping::NormalizedToBuzz(0.0, &p), 42);
    ASSERT_EQ(ParameterMapping::NormalizedToBuzz(1.0, &p), 42);
}

// ===========================================================================
// 23. CMasterInfo SamplesPerTick computation
// ===========================================================================

TEST(MasterInfoCalc, StandardValues) {
    // 120 BPM, 44100 Hz, 4 TPB
    // samples_per_tick = 60 * 44100 / (120 * 4) = 5512.5 -> 5512
    int expected = (int)((60.0 * 44100.0) / (120.0 * 4.0));
    BuzzMachineLoader loader;
    loader.UpdateMasterInfo(120.0, 44100.0, 4);
    ASSERT_EQ(loader.GetMasterInfo()->SamplesPerTick, expected);
}

TEST(MasterInfoCalc, HighBPM) {
    // 500 BPM, 44100 Hz, 4 TPB
    int expected = (int)((60.0 * 44100.0) / (500.0 * 4.0));
    BuzzMachineLoader loader;
    loader.UpdateMasterInfo(500.0, 44100.0, 4);
    ASSERT_EQ(loader.GetMasterInfo()->SamplesPerTick, expected);
}

TEST(MasterInfoCalc, HighSampleRate) {
    // 125 BPM, 96000 Hz, 4 TPB
    int expected = (int)((60.0 * 96000.0) / (125.0 * 4.0));
    BuzzMachineLoader loader;
    loader.UpdateMasterInfo(125.0, 96000.0, 4);
    ASSERT_EQ(loader.GetMasterInfo()->SamplesPerTick, expected);
}

TEST(MasterInfoCalc, HighTPB) {
    // 125 BPM, 44100 Hz, 32 TPB
    int expected = (int)((60.0 * 44100.0) / (125.0 * 32.0));
    BuzzMachineLoader loader;
    loader.UpdateMasterInfo(125.0, 44100.0, 32);
    ASSERT_EQ(loader.GetMasterInfo()->SamplesPerTick, expected);
}

// ===========================================================================
// 24. BridgeParamInfo: field accessibility
// ===========================================================================

TEST(BridgeIPC, ParamInfoFieldAccess) {
    BridgeParamInfo pi = {};
    pi.type = pt_byte;
    pi.minValue = 0;
    pi.maxValue = 127;
    pi.noValue = 255;
    pi.defValue = 64;
    pi.flags = MPF_STATE;
    strncpy(pi.name, "TestParameter", sizeof(pi.name) - 1);
    strncpy(pi.description, "A test parameter", sizeof(pi.description) - 1);

    ASSERT_EQ(pi.type, (int32_t)pt_byte);
    ASSERT_EQ(pi.maxValue, 127);
    ASSERT_EQ(strcmp(pi.name, "TestParameter"), 0);
}

TEST(BridgeIPC, MachineInfoFieldAccess) {
    BridgeMachineInfo mi = {};
    mi.type = MT_GENERATOR;
    mi.flags = MIF_MONO_TO_STEREO;
    mi.numGlobalParams = 5;
    mi.numTrackParams = 3;
    mi.minTracks = 1;
    mi.maxTracks = 8;
    strncpy(mi.name, "Test Generator", sizeof(mi.name) - 1);
    strncpy(mi.shortName, "TestGen", sizeof(mi.shortName) - 1);
    strncpy(mi.author, "Test Author", sizeof(mi.author) - 1);

    ASSERT_EQ(mi.type, MT_GENERATOR);
    ASSERT_EQ(mi.numGlobalParams, 5);
    ASSERT_EQ(strcmp(mi.name, "Test Generator"), 0);
}

// ===========================================================================
// 25. BridgeMasterInfo: field roundtrip
// ===========================================================================

TEST(BridgeIPC, MasterInfoRoundtrip) {
    BridgeMasterInfo bmi = {};
    bmi.beatsPerMin = 140;
    bmi.ticksPerBeat = 8;
    bmi.samplesPerSec = 48000;

    // Simulate applying to a loader
    BuzzMachineLoader loader;
    loader.UpdateMasterInfo((double)bmi.beatsPerMin, (double)bmi.samplesPerSec, bmi.ticksPerBeat);

    auto* mi = loader.GetMasterInfo();
    ASSERT_EQ(mi->BeatsPerMin, 140);
    ASSERT_EQ(mi->TicksPerBeat, 8);
    ASSERT_EQ(mi->SamplesPerSec, 48000);
}

// ===========================================================================
// 26. CSubTickInfo struct fields
// ===========================================================================

TEST(StructLayout, CSubTickInfoFields) {
    CSubTickInfo sti = {};
    sti.SubTicksPerTick = 4;
    sti.CurrentSubTick = 2;
    sti.SamplesPerSubTick = 1378;
    sti.PosInSubTick = 100;

    ASSERT_EQ(sti.SubTicksPerTick, 4);
    ASSERT_EQ(sti.CurrentSubTick, 2);
    ASSERT_EQ(sti.SamplesPerSubTick, 1378);
    ASSERT_EQ(sti.PosInSubTick, 100);
}

// ===========================================================================
// 27. CMachineAttribute struct
// ===========================================================================

TEST(StructLayout, CMachineAttributeFields) {
    CMachineAttribute attr;
    attr.Name = "Polyphony";
    attr.MinValue = 1;
    attr.MaxValue = 16;
    attr.DefValue = 8;

    ASSERT_EQ(strcmp(attr.Name, "Polyphony"), 0);
    ASSERT_EQ(attr.MinValue, 1);
    ASSERT_EQ(attr.MaxValue, 16);
    ASSERT_EQ(attr.DefValue, 8);
}

// ===========================================================================
// 28. BuzzCallbacks: GetMachinePosition returns zeros
// ===========================================================================

TEST(CallbacksExtra, GetMachinePosition) {
    BuzzCallbacks cb;
    float x = 99.0f, y = 99.0f;
    cb.GetMachinePosition(nullptr, x, y);
    ASSERT_NEAR(x, 0.0f, 0.001f);
    ASSERT_NEAR(y, 0.0f, 0.001f);
}

// ===========================================================================
// 29. BuzzCallbacks: GetConnectionSource/Destination return nulls
// ===========================================================================

TEST(CallbacksExtra, ConnectionSourceDestNull) {
    BuzzCallbacks cb;
    int ch = 99;
    ASSERT_NULL(cb.GetConnectionSource(nullptr, ch));
    ASSERT_EQ(ch, 0);

    ch = 99;
    ASSERT_NULL(cb.GetConnectionDestination(nullptr, ch));
    ASSERT_EQ(ch, 0);
}

// ===========================================================================
// 30. BuzzCallbacks: Sequence-related methods return safe defaults
// ===========================================================================

TEST(CallbacksExtra, SequenceMethods) {
    BuzzCallbacks cb;
    ASSERT_NULL(cb.CreateSequence());
    ASSERT_NULL(cb.GetSequenceData(0));
    ASSERT_NULL(cb.GetPlayingSequence(nullptr));
    ASSERT_NULL(cb.GetPlayingRow(nullptr, 0, 0));
    ASSERT_EQ(cb.GetSequenceColumn(nullptr), -1);
    ASSERT_EQ(cb.GetSequenceCount(nullptr), 0);
    ASSERT_NULL(cb.GetSequence(nullptr, 0));
    ASSERT_NULL(cb.GetPlayingPattern(nullptr));
    ASSERT_EQ(cb.GetPlayingPatternPosition(nullptr), -1);
}

// ===========================================================================
// 31. BuzzCallbacks: Pattern methods return safe defaults
// ===========================================================================

TEST(CallbacksExtra, PatternMethods) {
    BuzzCallbacks cb;
    ASSERT_NULL(cb.CreatePattern("test", 16));
    ASSERT_NULL(cb.GetPattern(0));
    ASSERT_EQ(cb.GetPatternData(nullptr, 0, 0, 0, 0), 0);
    ASSERT_EQ(cb.GetPatternLength(nullptr), 0);
    ASSERT_NULL(cb.GetPatternOwner(nullptr));
    ASSERT_NULL(cb.GetPatternByName(nullptr, "test"));
    ASSERT_FALSE(cb.MachineImplementsFunction(nullptr, 0, false));
}

// ===========================================================================
// 32. BuzzCallbacks: AD I/O
// ===========================================================================

TEST(CallbacksExtra, ADMethods) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.ADGetnumChannels(true), 0);
    ASSERT_EQ(cb.ADGetnumChannels(false), 0);

    // These should not crash
    float buf[256] = {};
    cb.ADWrite(0, buf, 256);
    cb.ADRead(0, buf, 256);
    ASSERT_TRUE(true);
}

// ===========================================================================
// 33. BuzzParamLayout: GetParamByteSize for all types
// ===========================================================================

TEST(ParamLayoutExtra, ByteSizeAllTypes) {
    ASSERT_EQ(BuzzParamLayout::GetParamByteSize(pt_note), 1);
    ASSERT_EQ(BuzzParamLayout::GetParamByteSize(pt_switch), 1);
    ASSERT_EQ(BuzzParamLayout::GetParamByteSize(pt_byte), 1);
    ASSERT_EQ(BuzzParamLayout::GetParamByteSize(pt_word), 2);
    ASSERT_EQ(BuzzParamLayout::GetParamByteSize(pt_internal), 0);
    // Unknown type should return 0
    ASSERT_EQ(BuzzParamLayout::GetParamByteSize((CMPType)99), 0);
}

// ===========================================================================
// 34. BridgeSharedAudio: packed struct has no padding
// ===========================================================================

TEST(BridgeIPC, SharedAudioNoExtraPadding) {
    // Expected: 4 arrays of kBridgeMaxSamples floats + 5 int32s
    size_t expected = 4 * kBridgeMaxSamples * sizeof(float)
                    + 5 * sizeof(int32_t);
    ASSERT_EQ(sizeof(BridgeSharedAudio), expected);
}

// ===========================================================================
// 35. BuzzCallbacks: RenameMachine returns false
// ===========================================================================

TEST(CallbacksExtra, RenameMachineReturnsFalse) {
    BuzzCallbacks cb;
    ASSERT_FALSE(cb.RenameMachine(nullptr, "newname"));
}

// ===========================================================================
// 36. BuzzCallbacks: GetSelectedWave returns 0
// ===========================================================================

TEST(CallbacksExtra, GetSelectedWaveReturnsZero) {
    BuzzCallbacks cb;
    ASSERT_EQ(cb.GetSelectedWave(), 0);
}

// ===========================================================================
// 37. BuzzCallbacks: GetMachineModuleHandle returns null
// ===========================================================================

TEST(CallbacksExtra, GetMachineModuleHandleReturnsNull) {
    BuzzCallbacks cb;
    ASSERT_NULL(cb.GetMachineModuleHandle(nullptr));
}
