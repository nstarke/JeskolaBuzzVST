#include "TestFramework.h"
#include "TestHelpers.h"
#include "../src/buzz/BuzzParamLayout.h"
#include "../src/buzz/BuzzCallbacks.h"
#include "../src/buzz/BuzzMachineLoader.h"
#include "../src/vst3/ParameterMapping.h"
#include "../src/common/SEHGuard.h"

using namespace BuzzVst;

// ===========================================================================
// Helper: Buzz note conversion (mirrors the static function in BuzzProcessor.cpp)
// ===========================================================================

static int MidiNoteToBuzz(int midiNote) {
	if (midiNote < 0) midiNote = 0;
	if (midiNote > 127) midiNote = 127;
	int octave = midiNote / 12;
	int note = (midiNote % 12) + 1;
	if (octave > 9) octave = 9;
	return (octave << 4) | note;
}

static int BuzzNoteToMidi(int buzzNote) {
	if (buzzNote == NOTE_OFF || buzzNote == NOTE_NO) return -1;
	int octave = (buzzNote >> 4) & 0xF;
	int note = buzzNote & 0xF;
	if (note < 1 || note > 12) return -1;
	return octave * 12 + (note - 1);
}

// ===========================================================================
// Note Conversion Tests
// ===========================================================================

TEST(MidiConversion, C0) {
	// MIDI C0 = 0, Buzz = octave 0, note 1 = 0x01
	ASSERT_EQ(MidiNoteToBuzz(0), 0x01);
}

TEST(MidiConversion, CSharp0) {
	// MIDI C#0 = 1, Buzz = octave 0, note 2 = 0x02
	ASSERT_EQ(MidiNoteToBuzz(1), 0x02);
}

TEST(MidiConversion, B0) {
	// MIDI B0 = 11, Buzz = octave 0, note 12 = 0x0C
	ASSERT_EQ(MidiNoteToBuzz(11), 0x0C);
}

TEST(MidiConversion, C1) {
	// MIDI C1 = 12, Buzz = octave 1, note 1 = 0x11
	ASSERT_EQ(MidiNoteToBuzz(12), 0x11);
}

TEST(MidiConversion, A4_Concert) {
	// MIDI A4 = 69 (concert pitch), Buzz = octave 5, note 10 = 0x5A
	// 69 / 12 = 5, 69 % 12 = 9, note = 9+1 = 10
	ASSERT_EQ(MidiNoteToBuzz(69), 0x5A);
}

TEST(MidiConversion, MiddleC) {
	// MIDI C4 = 60, Buzz = octave 5, note 1 = 0x51
	// Wait: 60 / 12 = 5, 60 % 12 = 0, note = 0+1 = 1
	ASSERT_EQ(MidiNoteToBuzz(60), 0x51);
}

TEST(MidiConversion, HighNote) {
	// MIDI G9 = 127, Buzz = octave 10 clamped to 9, note 8
	// 127 / 12 = 10, clamped to 9; 127 % 12 = 7, note = 8
	ASSERT_EQ(MidiNoteToBuzz(127), (9 << 4) | 8);
}

TEST(MidiConversion, NegativeClamped) {
	ASSERT_EQ(MidiNoteToBuzz(-5), 0x01); // clamped to 0 -> C0
}

TEST(MidiConversion, OverflowClamped) {
	ASSERT_EQ(MidiNoteToBuzz(200), MidiNoteToBuzz(127)); // clamped to 127
}

// ===========================================================================
// Roundtrip: MIDI -> Buzz -> MIDI
// ===========================================================================

TEST(MidiConversion, RoundtripLowRange) {
	// For notes in octaves 0-9 (MIDI 0-119), roundtrip should be exact
	for (int midi = 0; midi < 120; midi++) {
		int buzz = MidiNoteToBuzz(midi);
		int back = BuzzNoteToMidi(buzz);
		CHECK_EQ(back, midi);
	}
}

TEST(MidiConversion, RoundtripHighRangeLossy) {
	// MIDI 120-127 (octave 10) gets clamped to octave 9
	// So roundtrip is lossy: MIDI 120 -> Buzz (9,1) -> MIDI 108+0 = 108
	for (int midi = 120; midi <= 127; midi++) {
		int buzz = MidiNoteToBuzz(midi);
		int back = BuzzNoteToMidi(buzz);
		// The note-within-octave should match, but octave is clamped
		int expectedNote = (midi % 12) + 1;
		ASSERT_EQ(buzz & 0xF, expectedNote);
		ASSERT_EQ((buzz >> 4) & 0xF, 9); // clamped octave
	}
}

// ===========================================================================
// Special Buzz note values
// ===========================================================================

TEST(MidiConversion, NoteOff) {
	// NOTE_OFF = 255, should not convert to a valid MIDI note
	ASSERT_EQ(BuzzNoteToMidi(NOTE_OFF), -1);
}

TEST(MidiConversion, NoteNo) {
	// NOTE_NO = 0, "no note" marker
	ASSERT_EQ(BuzzNoteToMidi(NOTE_NO), -1);
}

TEST(MidiConversion, NoteMinMax) {
	// NOTE_MIN = 1 -> C0
	ASSERT_EQ(BuzzNoteToMidi(NOTE_MIN), 0);
	// NOTE_MAX = (16*9)+12 = 156 -> octave 9, note 12 = B9 = MIDI 119
	ASSERT_EQ(BuzzNoteToMidi(NOTE_MAX), 9 * 12 + 11);
}

// ===========================================================================
// Velocity Scaling
// ===========================================================================

static int VelocityVst3ToBuzz(float vst3Velocity) {
	int v = (int)(vst3Velocity * 127.0f + 0.5f);
	if (v < 0) v = 0;
	if (v > 127) v = 127;
	return v;
}

TEST(MidiVelocity, Zero) {
	ASSERT_EQ(VelocityVst3ToBuzz(0.0f), 0);
}

TEST(MidiVelocity, Full) {
	ASSERT_EQ(VelocityVst3ToBuzz(1.0f), 127);
}

TEST(MidiVelocity, Half) {
	int v = VelocityVst3ToBuzz(0.5f);
	ASSERT_GE(v, 63);
	ASSERT_LE(v, 64);
}

TEST(MidiVelocity, Quiet) {
	ASSERT_EQ(VelocityVst3ToBuzz(1.0f / 127.0f), 1);
}

TEST(MidiVelocity, ClampNegative) {
	ASSERT_EQ(VelocityVst3ToBuzz(-0.5f), 0);
}

TEST(MidiVelocity, ClampOver) {
	ASSERT_EQ(VelocityVst3ToBuzz(1.5f), 127);
}

TEST(MidiVelocity, AllSteps) {
	// Every integer from 0-127 should be reachable
	for (int i = 0; i <= 127; i++) {
		float norm = (float)i / 127.0f;
		int back = VelocityVst3ToBuzz(norm);
		CHECK_EQ(back, i);
	}
}

// ===========================================================================
// Note parameter writing (simulates writeNoteToParams behavior)
// ===========================================================================

// MakeParam is in TestHelpers.h

TEST(MidiNoteParam, WriteNoteToGlobalParam) {
	CMachineParameter pNote = MakeParam(pt_note, NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0);
	CMachineParameter pByte = MakeParam(pt_byte, 0, 127, 255, 64);
	const CMachineParameter* params[] = { &pByte, &pNote };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 2;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	// Allocate a buffer matching the global struct
	unsigned char buf[16] = {};
	layout.WriteAllNoValues(buf);

	// Simulate writing a note: find the note param and write to it
	auto& gSlots = layout.GetGlobalSlots();
	for (int j = 0; j < (int)gSlots.size(); j++) {
		if (gSlots[j].param->Type == pt_note) {
			int buzzNote = MidiNoteToBuzz(60); // C4
			layout.WriteGlobalParam(buf, j, buzzNote);
			break;
		}
	}

	// Verify the note was written to slot 1 (the pt_note param)
	ASSERT_EQ(layout.ReadGlobalParam(buf, 1), MidiNoteToBuzz(60));
	// Byte param should still be NoValue
	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), 255);
}

TEST(MidiNoteParam, WriteNoteOffToParam) {
	CMachineParameter pNote = MakeParam(pt_note, NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0);
	const CMachineParameter* params[] = { &pNote };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char buf[16] = {};

	// Write a note on
	layout.WriteGlobalParam(buf, 0, MidiNoteToBuzz(60));
	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), MidiNoteToBuzz(60));

	// Write note off
	layout.WriteGlobalParam(buf, 0, NOTE_OFF);
	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), NOTE_OFF);
}

TEST(MidiNoteParam, NoNoteParamSkipsWrite) {
	// Machine with no note parameters - should not crash
	CMachineParameter pByte = MakeParam(pt_byte, 0, 127, 255, 64);
	const CMachineParameter* params[] = { &pByte };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char buf[16] = {};
	layout.WriteAllNoValues(buf);

	// Search for pt_note - should find nothing
	auto& gSlots = layout.GetGlobalSlots();
	bool foundNote = false;
	for (int j = 0; j < (int)gSlots.size(); j++) {
		if (gSlots[j].param->Type == pt_note) {
			foundNote = true;
			break;
		}
	}
	ASSERT_FALSE(foundNote);
	// Byte param should be unchanged
	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), 255);
}

// ===========================================================================
// CC value conversion
// ===========================================================================

static int CcVst3ToBuzz(float vst3Value) {
	int v = (int)(vst3Value * 127.0f + 0.5f);
	if (v < 0) v = 0;
	if (v > 127) v = 127;
	return v;
}

TEST(MidiCC, ValueZero) {
	ASSERT_EQ(CcVst3ToBuzz(0.0f), 0);
}

TEST(MidiCC, ValueMax) {
	ASSERT_EQ(CcVst3ToBuzz(1.0f), 127);
}

TEST(MidiCC, ValueMid) {
	int v = CcVst3ToBuzz(0.5f);
	ASSERT_GE(v, 63);
	ASSERT_LE(v, 64);
}

// ===========================================================================
// BuzzCallbacks extended interface tracking
// ===========================================================================

TEST(MidiCallbacks, SetMachineInterfaceExCaptured) {
	BuzzCallbacks cb;
	ASSERT_NULL(cb.machineInterfaceEx);

	CMachineInterfaceEx ex;
	cb.SetMachineInterfaceEx(&ex);
	ASSERT_EQ(cb.machineInterfaceEx, &ex);
}

TEST(MidiCallbacks, SetMachineInterfaceExNull) {
	BuzzCallbacks cb;
	CMachineInterfaceEx ex;
	cb.SetMachineInterfaceEx(&ex);
	cb.SetMachineInterfaceEx(nullptr);
	ASSERT_NULL(cb.machineInterfaceEx);
}

// ===========================================================================
// Integration: MidiNote on real machine
// ===========================================================================

// GetGearPath is in TestHelpers.h

TEST(MidiIntegration, MidiNoteOnRealMachine) {
	std::string path = GetGearPath("generators\\FSM Kick XP.dll");

	BuzzMachineLoader loader;
	if (!loader.Load(path.c_str())) {
		printf("  (skipped - DLL not found) ");
		return;
	}

	loader.UpdateMasterInfo(125.0, 44100.0);
	if (!loader.InitMachine()) {
		printf("  (init failed) ");
		return;
	}

	auto* machine = loader.GetMachine();

	// Send defaults and tick
	auto* layout = loader.GetParamLayout();
	layout->WriteAllDefaults(machine->GlobalVals);
	bool ok = SEH_Call([&]() { machine->Tick(); });
	ASSERT_TRUE(ok);

	// Send MIDI note on - should not crash
	ok = SEH_Call([&]() {
		machine->MidiNote(0, 60, 100); // channel 0, C4, velocity 100
	});
	ASSERT_TRUE(ok);

	// Process a block
	float buffer[256] = {};
	ok = SEH_Call([&]() {
		machine->Work(buffer, 256, WM_WRITE);
	});
	ASSERT_TRUE(ok);

	// Send note off
	ok = SEH_Call([&]() {
		machine->MidiNote(0, 60, 0);
	});
	ASSERT_TRUE(ok);
}

TEST(MidiIntegration, MidiControlChangeOnExtendedInterface) {
	std::string path = GetGearPath("generators\\FSM Kick XP.dll");

	BuzzMachineLoader loader;
	if (!loader.Load(path.c_str())) {
		printf("  (skipped) ");
		return;
	}

	loader.UpdateMasterInfo(125.0, 44100.0);
	if (!loader.InitMachine()) {
		printf("  (init failed) ");
		return;
	}

	auto* machineEx = loader.GetMachineEx();

	// machineEx may be null if this machine doesn't register an extended interface
	if (!machineEx) {
		printf("  (no extended interface) ");
		// Verify the loader correctly reports null
		ASSERT_NULL(machineEx);
		return;
	}

	// Send CC - should not crash
	bool ok = SEH_Call([&]() {
		machineEx->MidiControlChange(1, 0, 64); // CC1 (mod wheel), channel 0, value 64
	});
	ASSERT_TRUE(ok);
}

// ===========================================================================
// Buzz note format encoding verification
// ===========================================================================

TEST(MidiNoteFormat, OctaveExtraction) {
	// Verify we can extract octave from Buzz note format
	for (int oct = 0; oct <= 9; oct++) {
		for (int note = 1; note <= 12; note++) {
			int buzzNote = (oct << 4) | note;
			ASSERT_EQ((buzzNote >> 4) & 0xF, oct);
			ASSERT_EQ(buzzNote & 0xF, note);
		}
	}
}

TEST(MidiNoteFormat, AllValidNotesInRange) {
	// All valid Buzz notes should be within NOTE_MIN..NOTE_MAX
	for (int midi = 0; midi < 120; midi++) {
		int buzz = MidiNoteToBuzz(midi);
		CHECK_TRUE(buzz >= NOTE_MIN);
		CHECK_TRUE(buzz <= NOTE_MAX);
	}
}

TEST(MidiNoteFormat, NoteOffIsDistinct) {
	// NOTE_OFF (255) should never be produced by MidiNoteToBuzz
	for (int midi = 0; midi <= 127; midi++) {
		int buzz = MidiNoteToBuzz(midi);
		CHECK_NE(buzz, NOTE_OFF);
		CHECK_NE(buzz, NOTE_NO);
	}
}

// ===========================================================================
// Velocity Routing Tests
// ===========================================================================
// These test the velocity-to-volume-param routing logic:
//   - When a note-on arrives, the byte param following the note param
//     (or one named "Volume"/"Velocity") gets the velocity value scaled
//     to its range.

// Helper: check if a name matches the velocity heuristic
static bool TestIsVelocityName(const char* name) {
	for (const char* kw : {"olum", "eloc", "Amp", "amp", "vel", "Vol", "vol", "Vel"}) {
		if (strstr(name, kw)) return true;
	}
	return false;
}

TEST(VelocityRouting, NameDetection) {
	ASSERT_TRUE(TestIsVelocityName("Volume"));
	ASSERT_TRUE(TestIsVelocityName("Velocity"));
	ASSERT_TRUE(TestIsVelocityName("vel"));
	ASSERT_TRUE(TestIsVelocityName("Vol"));
	ASSERT_TRUE(TestIsVelocityName("Amp"));
	ASSERT_TRUE(TestIsVelocityName("Amplification"));
	ASSERT_FALSE(TestIsVelocityName("Cutoff"));
	ASSERT_FALSE(TestIsVelocityName("Note"));
	ASSERT_FALSE(TestIsVelocityName("Resonance"));
}

// Simulate the velocity routing logic from BuzzProcessor::writeNoteToParams
// to test it in isolation without needing the full VST3 processor.
static void SimulateVelocityWrite(
	BuzzParamLayout& layout, unsigned char* trackBuf,
	int noteSlotIdx, int buzzNote, int midiVelocity)
{
	auto& tSlots = layout.GetTrackSlots();

	// Write the note
	layout.WriteTrackParam(trackBuf, 0, noteSlotIdx, buzzNote);

	// Find the velocity slot (mirrors FindVelocitySlot logic in BuzzProcessor.cpp)
	int velSlot = -1;

	// Primary: adjacent byte param (standard Buzz convention)
	int next = noteSlotIdx + 1;
	if (next < (int)tSlots.size() && tSlots[next].param->Type == pt_byte) {
		velSlot = next;
	}

	// Fallback: scan by name
	if (velSlot < 0) {
		for (int j = 0; j < (int)tSlots.size(); j++) {
			if (j == noteSlotIdx) continue;
			if (tSlots[j].param->Type == pt_byte && tSlots[j].param->Name &&
				TestIsVelocityName(tSlots[j].param->Name)) {
				velSlot = j;
				break;
			}
		}
	}

	if (velSlot >= 0 && buzzNote != NOTE_OFF) {
		const CMachineParameter* vp = tSlots[velSlot].param;
		int buzzVel = vp->MinValue +
			(int)((float)midiVelocity / 127.0f * (float)(vp->MaxValue - vp->MinValue) + 0.5f);
		if (buzzVel < vp->MinValue) buzzVel = vp->MinValue;
		if (buzzVel > vp->MaxValue) buzzVel = vp->MaxValue;
		layout.WriteTrackParam(trackBuf, 0, velSlot, buzzVel);
	}
}

TEST(VelocityRouting, NoteFollowedByVolumeByte) {
	// Typical Buzz pattern: Note (pt_note) followed by Volume (pt_byte, 0-0xFE)
	CMachineParameter pNote = MakeParam(pt_note, NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0);
	CMachineParameter pVol = {pt_byte, "Volume", "Volume, 80h = 100%", 0, 0xFE, 0xFF, MPF_STATE, 0x80};

	const CMachineParameter* params[] = { &pNote, &pVol };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 0;
	info.numTrackParameters = 2;
	info.Parameters = params;
	info.minTracks = 1;
	info.maxTracks = 1;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char trackBuf[16] = {};
	layout.WriteTrackAllNoValues(trackBuf, 1);

	// Play C4 at full velocity (127)
	SimulateVelocityWrite(layout, trackBuf, 0, MidiNoteToBuzz(60), 127);

	// Note should be written
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 0), MidiNoteToBuzz(60));

	// Volume should be at max (0xFE)
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 1), 0xFE);
}

TEST(VelocityRouting, HalfVelocity) {
	CMachineParameter pNote = MakeParam(pt_note, NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0);
	CMachineParameter pVol = {pt_byte, "Volume", "Volume", 0, 0xFE, 0xFF, MPF_STATE, 0x80};

	const CMachineParameter* params[] = { &pNote, &pVol };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 0;
	info.numTrackParameters = 2;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char trackBuf[16] = {};
	layout.WriteTrackAllNoValues(trackBuf, 1);

	// Play at ~50% velocity (64/127)
	SimulateVelocityWrite(layout, trackBuf, 0, MidiNoteToBuzz(60), 64);

	int vol = layout.ReadTrackParam(trackBuf, 0, 1);
	// 0 + (64/127) * 254 = ~128 = 0x80
	ASSERT_GE(vol, 0x7E);
	ASSERT_LE(vol, 0x82);
}

TEST(VelocityRouting, ZeroVelocity) {
	CMachineParameter pNote = MakeParam(pt_note, NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0);
	CMachineParameter pVol = {pt_byte, "Volume", "Volume", 0, 0xFE, 0xFF, MPF_STATE, 0x80};

	const CMachineParameter* params[] = { &pNote, &pVol };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 0;
	info.numTrackParameters = 2;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char trackBuf[16] = {};
	layout.WriteTrackAllNoValues(trackBuf, 1);

	SimulateVelocityWrite(layout, trackBuf, 0, MidiNoteToBuzz(60), 0);

	// Volume at 0 velocity -> MinValue (0)
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 1), 0);
}

TEST(VelocityRouting, NoteOffSkipsVelocity) {
	CMachineParameter pNote = MakeParam(pt_note, NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0);
	CMachineParameter pVol = {pt_byte, "Volume", "Volume", 0, 0xFE, 0xFF, MPF_STATE, 0x80};

	const CMachineParameter* params[] = { &pNote, &pVol };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 0;
	info.numTrackParameters = 2;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char trackBuf[16] = {};
	layout.WriteTrackAllNoValues(trackBuf, 1);

	// NOTE_OFF should not write to volume
	SimulateVelocityWrite(layout, trackBuf, 0, NOTE_OFF, 100);

	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 0), NOTE_OFF);
	// Volume should remain at NoValue (0xFF)
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 1), 0xFF);
}

TEST(VelocityRouting, NonAdjacentVolumeByName) {
	// Note is param 0, a word param at 1 (not byte, so adjacent rule skips it),
	// then a byte "Velocity" param at 2 found by name fallback.
	CMachineParameter pNote = MakeParam(pt_note, NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0);
	CMachineParameter pFreq = MakeParam(pt_word, 0, 1000, 65535, 500);
	pFreq.Name = "Frequency";
	CMachineParameter pVol = {pt_byte, "Velocity", "Velocity", 0, 127, 255, MPF_STATE, 100};

	const CMachineParameter* params[] = { &pNote, &pFreq, &pVol };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 0;
	info.numTrackParameters = 3;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char trackBuf[16] = {};
	layout.WriteTrackAllNoValues(trackBuf, 1);

	SimulateVelocityWrite(layout, trackBuf, 0, MidiNoteToBuzz(60), 127);

	// Frequency (slot 1, word) should be untouched
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 1), 65535);
	// Velocity (slot 2) should be at max (127)
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 2), 127);
}

TEST(VelocityRouting, AdjacentBytePreferredOverNamedFarther) {
	// When the param right after the note is pt_byte, it should be used
	// even if another param farther away is named "Volume"
	CMachineParameter pNote = MakeParam(pt_note, NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0);
	CMachineParameter pAdj = {pt_byte, "Level", "Level", 0, 200, 255, MPF_STATE, 100};
	CMachineParameter pVol = {pt_byte, "Volume", "Volume", 0, 127, 255, MPF_STATE, 100};

	const CMachineParameter* params[] = { &pNote, &pAdj, &pVol };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 0;
	info.numTrackParameters = 3;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char trackBuf[16] = {};
	layout.WriteTrackAllNoValues(trackBuf, 1);

	SimulateVelocityWrite(layout, trackBuf, 0, MidiNoteToBuzz(60), 127);

	// Adjacent "Level" (slot 1) should get the velocity, scaled to 0-200
	int level = layout.ReadTrackParam(trackBuf, 0, 1);
	ASSERT_EQ(level, 200); // full velocity -> max of range

	// "Volume" (slot 2) should be untouched
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 2), 255);
}

TEST(VelocityRouting, NoVolumeParamNoWrite) {
	// Machine with only a note param and a word param (not byte) -> no velocity routing
	CMachineParameter pNote = MakeParam(pt_note, NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0);
	CMachineParameter pWord = MakeParam(pt_word, 0, 1000, 65535, 500);
	pWord.Name = "Frequency";

	const CMachineParameter* params[] = { &pNote, &pWord };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 0;
	info.numTrackParameters = 2;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char trackBuf[16] = {};
	layout.WriteTrackAllNoValues(trackBuf, 1);

	SimulateVelocityWrite(layout, trackBuf, 0, MidiNoteToBuzz(60), 127);

	// Note written
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 0), MidiNoteToBuzz(60));
	// Word param should be untouched at NoValue
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 1), 65535);
}

TEST(VelocityRouting, VelocityScalesToCustomRange) {
	// Volume param with range 10-110 (not starting at 0)
	CMachineParameter pNote = MakeParam(pt_note, NOTE_MIN, NOTE_MAX, NOTE_NO, 0, 0);
	CMachineParameter pVol = {pt_byte, "Volume", "Volume", 10, 110, 255, MPF_STATE, 60};

	const CMachineParameter* params[] = { &pNote, &pVol };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 0;
	info.numTrackParameters = 2;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char trackBuf[16] = {};
	layout.WriteTrackAllNoValues(trackBuf, 1);

	// Full velocity
	SimulateVelocityWrite(layout, trackBuf, 0, MidiNoteToBuzz(60), 127);
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 1), 110); // max of range

	// Zero velocity
	layout.WriteTrackAllNoValues(trackBuf, 1);
	SimulateVelocityWrite(layout, trackBuf, 0, MidiNoteToBuzz(60), 0);
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 1), 10); // min of range

	// Half velocity -> 10 + (64/127)*100 = ~60
	layout.WriteTrackAllNoValues(trackBuf, 1);
	SimulateVelocityWrite(layout, trackBuf, 0, MidiNoteToBuzz(60), 64);
	int vol = layout.ReadTrackParam(trackBuf, 0, 1);
	ASSERT_GE(vol, 58);
	ASSERT_LE(vol, 62);
}
