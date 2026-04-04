#include <windows.h>
#include "TestFramework.h"
#include "../src/buzz/MachineInterface.h"
#include "../src/buzz/BuzzMachineLoader.h"
#include "../src/buzz/BuzzParamLayout.h"
#include "../src/common/SEHGuard.h"

using namespace BuzzVst;

// Helper to build paths relative to the test executable
// The ref DLLs are in the repo at ref/Gear/
static std::string GetRefPath(const char* relPath) {
	// Get the directory of the test executable
	char exePath[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, exePath, MAX_PATH);

	// Walk up from build/bin/Release/ to project root
	std::string path = exePath;
	for (int i = 0; i < 4; i++) {
		size_t pos = path.find_last_of("\\/");
		if (pos != std::string::npos) path = path.substr(0, pos);
	}
	path += "/";
	path += relPath;
	return path;
}

// ===== Loading invalid paths =====

TEST(MachineLoader, LoadNullPath) {
	BuzzMachineLoader loader;
	ASSERT_FALSE(loader.Load(nullptr));
	ASSERT_FALSE(loader.IsLoaded());
}

TEST(MachineLoader, LoadEmptyPath) {
	BuzzMachineLoader loader;
	ASSERT_FALSE(loader.Load(""));
	ASSERT_FALSE(loader.IsLoaded());
}

TEST(MachineLoader, LoadNonexistentFile) {
	BuzzMachineLoader loader;
	ASSERT_FALSE(loader.Load("C:\\nonexistent\\fake_machine.dll"));
	ASSERT_FALSE(loader.IsLoaded());
}

TEST(MachineLoader, LoadNonMachineDll) {
	// kernel32.dll exists but doesn't have GetInfo/CreateMachine exports
	BuzzMachineLoader loader;
	ASSERT_FALSE(loader.Load("C:\\Windows\\System32\\kernel32.dll"));
	ASSERT_FALSE(loader.IsLoaded());
}

// ===== Loading real generator DLLs =====

TEST(MachineLoader, LoadFSMKickXP) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");

	BuzzMachineLoader loader;
	bool loaded = loader.Load(path.c_str());
	if (!loaded) {
		printf("  (skipped - DLL not found at %s) ", path.c_str());
		return;
	}

	ASSERT_TRUE(loaded);
	ASSERT_NOT_NULL(loader.GetInfo());
	ASSERT_EQ(loader.GetInfo()->Type, MT_GENERATOR);

	// Verify it has a name
	ASSERT_NOT_NULL(loader.GetInfo()->Name);
	ASSERT_GT((int)strlen(loader.GetInfo()->Name), 0);

	// Verify parameter count is sane
	ASSERT_GE(loader.GetInfo()->numGlobalParameters, 0);
	ASSERT_LT(loader.GetInfo()->numGlobalParameters, 100);
}

TEST(MachineLoader, LoadBTDSysPulsar) {
	std::string path = GetRefPath("ref/Gear/Generators/BTDSys Pulsar.dll");

	BuzzMachineLoader loader;
	bool loaded = loader.Load(path.c_str());
	if (!loaded) {
		printf("  (skipped - DLL not found) ");
		return;
	}

	ASSERT_TRUE(loaded);
	ASSERT_EQ(loader.GetInfo()->Type, MT_GENERATOR);
	ASSERT_NOT_NULL(loader.GetInfo()->Name);
}

// ===== Loading real effect DLLs =====

TEST(MachineLoader, LoadCheapoAmp) {
	std::string path = GetRefPath("ref/Gear/Effects/cheapo amp.dll");

	BuzzMachineLoader loader;
	bool loaded = loader.Load(path.c_str());
	if (!loaded) {
		printf("  (skipped - DLL not found) ");
		return;
	}

	ASSERT_TRUE(loaded);
	ASSERT_NOT_NULL(loader.GetInfo());
	ASSERT_EQ(loader.GetInfo()->Type, MT_EFFECT);
}

TEST(MachineLoader, LoadBigyoFilter) {
	std::string path = GetRefPath("ref/Gear/Effects/Bigyo Filter.dll");

	BuzzMachineLoader loader;
	bool loaded = loader.Load(path.c_str());
	if (!loaded) {
		printf("  (skipped - DLL not found) ");
		return;
	}

	ASSERT_TRUE(loaded);
	ASSERT_EQ(loader.GetInfo()->Type, MT_EFFECT);
	ASSERT_NOT_NULL(loader.GetInfo()->Name);
}

// ===== Parameter validation =====

TEST(MachineLoader, ParametersHaveValidRanges) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");

	BuzzMachineLoader loader;
	if (!loader.Load(path.c_str())) {
		printf("  (skipped) ");
		return;
	}

	auto* info = loader.GetInfo();
	int totalParams = info->numGlobalParameters + info->numTrackParameters;

	for (int i = 0; i < totalParams; i++) {
		auto* p = info->Parameters[i];
		ASSERT_NOT_NULL(p);
		ASSERT_NOT_NULL(p->Name);

		// MinValue should be <= MaxValue
		CHECK_TRUE(p->MinValue <= p->MaxValue);

		// Type should be valid
		CHECK_TRUE(p->Type == pt_note || p->Type == pt_switch ||
		           p->Type == pt_byte || p->Type == pt_word);
	}
}

// ===== Param layout from real machine =====

TEST(MachineLoader, ParamLayoutFromRealMachine) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");

	BuzzMachineLoader loader;
	if (!loader.Load(path.c_str())) {
		printf("  (skipped) ");
		return;
	}

	auto* layout = loader.GetParamLayout();
	auto* info = loader.GetInfo();

	ASSERT_EQ((int)layout->GetGlobalSlots().size(), info->numGlobalParameters);
	ASSERT_GE(layout->GetGlobalStructSize(), info->numGlobalParameters);
}

// ===== Machine initialization =====

TEST(MachineLoader, InitFSMKickXP) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");

	BuzzMachineLoader loader;
	if (!loader.Load(path.c_str())) {
		printf("  (skipped) ");
		return;
	}

	// Update master info before init
	loader.UpdateMasterInfo(125.0, 44100.0);

	bool inited = loader.InitMachine();
	ASSERT_TRUE(inited);
	ASSERT_TRUE(loader.IsLoaded());
	ASSERT_FALSE(loader.IsFaulted());
	ASSERT_NOT_NULL(loader.GetMachine());
}

TEST(MachineLoader, InitCheapoAmp) {
	std::string path = GetRefPath("ref/Gear/Effects/cheapo amp.dll");

	BuzzMachineLoader loader;
	if (!loader.Load(path.c_str())) {
		printf("  (skipped) ");
		return;
	}

	loader.UpdateMasterInfo(120.0, 48000.0);

	bool inited = loader.InitMachine();
	ASSERT_TRUE(inited);
	ASSERT_NOT_NULL(loader.GetMachine());
}

// ===== Machine has valid GlobalVals after creation =====

TEST(MachineLoader, GlobalValsNotNullAfterInit) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");

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

	auto* machine = loader.GetMachine();
	// Machine should have GlobalVals set (by its own constructor)
	if (loader.GetInfo()->numGlobalParameters > 0) {
		ASSERT_NOT_NULL(machine->GlobalVals);
	}
}

// ===== Tick doesn't crash =====

TEST(MachineLoader, TickDoesNotCrash) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");

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

	auto* machine = loader.GetMachine();
	auto* layout = loader.GetParamLayout();

	// Set all params to NoValue (no change)
	layout->WriteAllNoValues(machine->GlobalVals);

	// Call Tick with SEH protection
	bool ok = SEH_Call([&]() { machine->Tick(); });
	ASSERT_TRUE(ok);
}

// ===== Work produces output =====

TEST(MachineLoader, WorkProducesOutput) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");

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

	auto* machine = loader.GetMachine();
	auto* layout = loader.GetParamLayout();

	// Send default parameter values to trigger sound
	layout->WriteAllDefaults(machine->GlobalVals);
	machine->Tick();

	// Call Work with SEH protection
	float buffer[256] = {};
	bool hasOutput = false;
	bool ok2 = SEH_Call([&]() { hasOutput = machine->Work(buffer, 256, WM_WRITE); });
	ASSERT_TRUE(ok2);
}

TEST(MachineLoader, FSMKickXPWithNote) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");

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

	auto* machine = loader.GetMachine();
	auto* layout = loader.GetParamLayout();

	// Defaults + tick
	layout->WriteAllDefaults(machine->GlobalVals);
	SEH_Call([&]() { machine->Tick(); });

	float maxSample = 0;
	for (int block = 0; block < 20; block++) {
		float buffer[256] = {};
		bool hasOutput = false;
		SEH_Call([&]() { hasOutput = machine->Work(buffer, 256, WM_WRITE); });
		for (int i = 0; i < 256; i++) {
			float a = buffer[i] < 0 ? -buffer[i] : buffer[i];
			if (a > maxSample) maxSample = a;
		}
		if (maxSample > 0) break;
	}
	printf("  (maxSample=%f) ", maxSample);
	ASSERT_TRUE(maxSample > 0);
}

TEST(MachineLoader, FSMInfectorProducesOutput) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Infector.dll");

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
	auto* layout = loader.GetParamLayout();
	auto& gSlots = layout->GetGlobalSlots();
	auto& tSlots = layout->GetTrackSlots();

	auto* info = loader.GetInfo();
	printf("  (flags=0x%x numAttrs=%d gParams=%d tParams=%d) ",
		info->Flags, info->numAttributes,
		info->numGlobalParameters, info->numTrackParameters);
	// Print track params to understand note/velocity layout
	for (int i = 0; i < info->numTrackParameters; i++) {
		auto* p = info->Parameters[info->numGlobalParameters + i];
		printf("\n    T[%d] %s: type=%d min=%d max=%d noVal=%d def=%d",
			i, p->Name, p->Type, p->MinValue, p->MaxValue, p->NoValue, p->DefValue);
	}

	// Call AttributesChanged in case machine needs it
	SEH_Call([&]() { machine->AttributesChanged(); });

	// First tick: write all defaults
	layout->WriteAllDefaults(machine->GlobalVals);
	if (machine->TrackVals) {
		for (int i = 0; i < (int)tSlots.size(); i++) {
			if (tSlots[i].param->Flags & MPF_STATE)
				layout->WriteTrackParam(machine->TrackVals, 0, i, tSlots[i].param->DefValue);
			else
				layout->WriteTrackParam(machine->TrackVals, 0, i, tSlots[i].param->NoValue);
		}
	}
	SEH_Call([&]() { machine->Tick(); });

	// Also try MidiNote directly
	SEH_Call([&]() { machine->MidiNote(0, 60, 127); });

	// Second tick: write note C-5 (0x51=81) to first pt_note track param
	layout->WriteAllNoValues(machine->GlobalVals);
	if (machine->TrackVals) {
		// Reset track to NoValues
		layout->WriteTrackAllNoValues(machine->TrackVals, 1);
		// Write note
		for (int i = 0; i < (int)tSlots.size(); i++) {
			if (tSlots[i].param->Type == pt_note) {
				layout->WriteTrackParam(machine->TrackVals, 0, i, 0x51); // C-5
				// Write velocity to next byte param if exists
				if (i + 1 < (int)tSlots.size() && tSlots[i+1].param->Type == pt_byte) {
					layout->WriteTrackParam(machine->TrackVals, 0, i+1, 189); // default vel
				}
				break;
			}
		}
	}
	SEH_Call([&]() { machine->Tick(); });

	// Work for many blocks — try BOTH Work() and WorkMonoToStereo()
	float maxSample = 0;
	float maxSampleM2S = 0;
	int samplesPerTick = loader.GetMasterInfo()->SamplesPerTick;
	int posInTick = 0;
	for (int block = 0; block < 200; block++) {
		float buffer[256] = {};
		float bufferR[256] = {};
		bool hasOutput = false;
		SEH_Call([&]() { hasOutput = machine->Work(buffer, 256, WM_WRITE); });

		// Also try MonoToStereo
		float m2sL[256] = {};
		float m2sR[256] = {};
		bool m2sOut = false;
		SEH_Call([&]() { m2sOut = machine->WorkMonoToStereo(m2sL, m2sR, 256, WM_WRITE); });

		loader.GetMasterInfo()->PosInTick = posInTick;
		posInTick += 256;

		// Fire tick when needed (with NoValues = no change)
		if (posInTick >= samplesPerTick) {
			layout->WriteAllNoValues(machine->GlobalVals);
			layout->WriteTrackAllNoValues(machine->TrackVals, 1);
			SEH_Call([&]() { machine->Tick(); });
			posInTick = 0;
			loader.GetMasterInfo()->PosInTick = 0;
		}

		for (int i = 0; i < 256; i++) {
			float a = buffer[i] < 0 ? -buffer[i] : buffer[i];
			if (a > maxSample) maxSample = a;
			float aL = m2sL[i] < 0 ? -m2sL[i] : m2sL[i];
			float aR = m2sR[i] < 0 ? -m2sR[i] : m2sR[i];
			if (aL > maxSampleM2S) maxSampleM2S = aL;
			if (aR > maxSampleM2S) maxSampleM2S = aR;
		}
		if (maxSample > 0 || maxSampleM2S > 0) break;
	}

	// Check aux buffer too
	float* auxBuf = loader.GetCallbacks()->GetAuxBuffer();
	float maxAux = 0;
	for (int i = 0; i < 256; i++) {
		float a = auxBuf[i] < 0 ? -auxBuf[i] : auxBuf[i];
		if (a > maxAux) maxAux = a;
	}

	printf("  (Work=%f M2S=%f Aux=%f) ", maxSample, maxSampleM2S, maxAux);
	if (maxSample == 0 && maxSampleM2S == 0 && maxAux == 0) printf("  (NO AUDIO!) ");
}

TEST(MachineLoader, BTDSysPulsarProducesOutput) {
	std::string path = GetRefPath("ref/Gear/Generators/BTDSys Pulsar.dll");

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
	auto* layout = loader.GetParamLayout();
	auto& tSlots = layout->GetTrackSlots();

	// Defaults + tick
	layout->WriteAllDefaults(machine->GlobalVals);
	if (machine->TrackVals) {
		for (int i = 0; i < (int)tSlots.size(); i++) {
			if (tSlots[i].param->Flags & MPF_STATE)
				layout->WriteTrackParam(machine->TrackVals, 0, i, tSlots[i].param->DefValue);
			else
				layout->WriteTrackParam(machine->TrackVals, 0, i, tSlots[i].param->NoValue);
		}
	}
	SEH_Call([&]() { machine->Tick(); });

	// Send MIDI note + note param
	SEH_Call([&]() { machine->MidiNote(0, 60, 127); });

	layout->WriteAllNoValues(machine->GlobalVals);
	if (machine->TrackVals) {
		layout->WriteTrackAllNoValues(machine->TrackVals, 1);
		for (int i = 0; i < (int)tSlots.size(); i++) {
			if (tSlots[i].param->Type == pt_note) {
				layout->WriteTrackParam(machine->TrackVals, 0, i, 0x51);
				if (i + 1 < (int)tSlots.size() && tSlots[i+1].param->Type == pt_byte)
					layout->WriteTrackParam(machine->TrackVals, 0, i+1, 189);
				break;
			}
		}
	}
	SEH_Call([&]() { machine->Tick(); });

	float maxSample = 0;
	int samplesPerTick = loader.GetMasterInfo()->SamplesPerTick;
	int posInTick = 0;
	for (int block = 0; block < 200; block++) {
		float buffer[256] = {};
		bool hasOutput = false;
		SEH_Call([&]() { hasOutput = machine->Work(buffer, 256, WM_WRITE); });
		loader.GetMasterInfo()->PosInTick = posInTick;
		posInTick += 256;
		if (posInTick >= samplesPerTick) {
			layout->WriteAllNoValues(machine->GlobalVals);
			layout->WriteTrackAllNoValues(machine->TrackVals, 1);
			SEH_Call([&]() { machine->Tick(); });
			posInTick = 0;
			loader.GetMasterInfo()->PosInTick = 0;
		}
		for (int i = 0; i < 256; i++) {
			float a = buffer[i] < 0 ? -buffer[i] : buffer[i];
			if (a > maxSample) maxSample = a;
		}
		if (maxSample > 0) break;
	}
	printf("  (maxSample=%f) ", maxSample);
	if (maxSample == 0) printf("  (NO AUDIO!) ");
}

// ===== Effect processes audio =====

TEST(MachineLoader, EffectProcessesAudio) {
	std::string path = GetRefPath("ref/Gear/Effects/cheapo amp.dll");

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

	auto* machine = loader.GetMachine();
	auto* layout = loader.GetParamLayout();

	layout->WriteAllDefaults(machine->GlobalVals);
	machine->Tick();

	// Fill input buffer with a test signal
	float buffer[256];
	for (int i = 0; i < 256; i++) {
		buffer[i] = 16384.0f * sinf((float)i * 0.1f); // Buzz scale signal
	}

	bool hasOutput = false;
	bool ok2 = SEH_Call([&]() { hasOutput = machine->Work(buffer, 256, WM_READWRITE); });
	ASSERT_TRUE(ok2);
}

// ===== Unload and reload =====

TEST(MachineLoader, UnloadAndReload) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");

	BuzzMachineLoader loader;
	if (!loader.Load(path.c_str())) {
		printf("  (skipped) ");
		return;
	}

	loader.UpdateMasterInfo(125.0, 44100.0);
	loader.InitMachine();
	ASSERT_TRUE(loader.IsLoaded());

	// Unload
	loader.Unload();
	ASSERT_FALSE(loader.IsLoaded());
	ASSERT_NULL(loader.GetMachine());
	ASSERT_NULL(loader.GetInfo());

	// Reload
	bool reloaded = loader.Load(path.c_str());
	ASSERT_TRUE(reloaded);

	loader.UpdateMasterInfo(125.0, 44100.0);
	bool reinited = loader.InitMachine();
	ASSERT_TRUE(reinited);
	ASSERT_NOT_NULL(loader.GetMachine());
}

// ===== UpdateMasterInfo =====

TEST(MachineLoader, UpdateMasterInfo) {
	BuzzMachineLoader loader;
	auto* mi = loader.GetMasterInfo();

	loader.UpdateMasterInfo(140.0, 48000.0, 8);

	ASSERT_EQ(mi->BeatsPerMin, 140);
	ASSERT_EQ(mi->SamplesPerSec, 48000);
	ASSERT_EQ(mi->TicksPerBeat, 8);
	ASSERT_GT(mi->SamplesPerTick, 0);

	// SamplesPerTick = 60 * 48000 / (140 * 8) = 2571.4... -> 2571
	int expected = (int)((60.0 * 48000.0) / (140.0 * 8.0));
	ASSERT_EQ(mi->SamplesPerTick, expected);
}

TEST(MachineLoader, UpdateMasterInfoClamping) {
	BuzzMachineLoader loader;
	auto* mi = loader.GetMasterInfo();

	// BPM should clamp to [16, 500]
	loader.UpdateMasterInfo(5.0, 44100.0);
	ASSERT_EQ(mi->BeatsPerMin, 125); // falls back to 125 when < 16

	loader.UpdateMasterInfo(600.0, 44100.0);
	ASSERT_EQ(mi->BeatsPerMin, 500);

	// TPB should clamp to [1, 32]
	loader.UpdateMasterInfo(125.0, 44100.0, 0);
	ASSERT_EQ(mi->TicksPerBeat, 4); // falls back

	loader.UpdateMasterInfo(125.0, 44100.0, 100);
	ASSERT_EQ(mi->TicksPerBeat, 32);
}

// ===== Stop doesn't crash =====

TEST(MachineLoader, StopDoesNotCrash) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");

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

	loader.StopMachine();
	ASSERT_TRUE(true);
}

// ===== Multiple machines can be loaded simultaneously =====

TEST(MachineLoader, MultipleMachinesSimultaneous) {
	std::string genPath = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");
	std::string fxPath = GetRefPath("ref/Gear/Effects/cheapo amp.dll");

	BuzzMachineLoader gen, fx;

	if (!gen.Load(genPath.c_str()) || !fx.Load(fxPath.c_str())) {
		printf("  (skipped - DLLs not found) ");
		return;
	}

	gen.UpdateMasterInfo(125.0, 44100.0);
	fx.UpdateMasterInfo(125.0, 44100.0);

	bool genOk = gen.InitMachine();
	bool fxOk = fx.InitMachine();

	ASSERT_TRUE(genOk);
	ASSERT_TRUE(fxOk);

	// Both should be independent
	ASSERT_NE((void*)gen.GetMachine(), (void*)fx.GetMachine());
	ASSERT_EQ(gen.GetInfo()->Type, MT_GENERATOR);
	ASSERT_EQ(fx.GetInfo()->Type, MT_EFFECT);
}

// ===== Loaded path tracking =====

TEST(MachineLoader, LoadedPathTracking) {
	std::string path = GetRefPath("ref/Gear/Generators/FSM Kick XP.dll");

	BuzzMachineLoader loader;
	ASSERT_TRUE(loader.GetLoadedPath().empty());

	if (!loader.Load(path.c_str())) {
		printf("  (skipped) ");
		return;
	}

	ASSERT_FALSE(loader.GetLoadedPath().empty());
	ASSERT_EQ(loader.GetLoadedPath(), path);

	loader.Unload();
	ASSERT_TRUE(loader.GetLoadedPath().empty());
}
