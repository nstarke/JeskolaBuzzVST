// BuzzBridgeHost32.exe - 32-bit process that loads Buzz machine DLLs
// and processes audio on behalf of the 64-bit VST3 plugin.
//
// Usage: BuzzBridgeHost32.exe <session-id>
//
// Creates a named pipe and shared memory identified by session-id,
// then enters a command loop processing requests from the 64-bit plugin.

#include <windows.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "BridgeIPC.h"
#include "../buzz/MachineInterface.h"
#include "../buzz/BuzzMachineLoader.h"
#include "../buzz/BuzzParamLayout.h"
#include "../common/SEHGuard.h"

using namespace BuzzVst;

#include "../common/PatchMessageBoxes.h"

static BuzzMachineLoader g_loader;
static BridgePipe g_pipe;
static BridgeSharedMem g_sharedMem;
static std::string g_sessionId;
static bool g_running = true;
static int g_numTracks = 1;
static bool g_firstTick = true;
static int g_workCrashCount = 0;
static HANDLE g_hWorkReady = nullptr;  // Event: client signals "work ready"
static HANDLE g_hWorkDone = nullptr;   // Event: host signals "work done"

// Read a null-terminated string from the pipe
static bool ReadString(BridgePipe& pipe, uint32_t size, std::string& out) {
	if (size > 65536) return false;
	std::vector<char> buf(size + 1, 0);
	if (!pipe.ReadAll(buf.data(), size)) return false;
	buf[size] = 0;
	out = buf.data();
	return true;
}

static void HandleLoadDll(uint32_t payloadSize) {
	std::string path;
	if (!ReadString(g_pipe, payloadSize, path)) {
		g_pipe.SendResponse(kRespError);
		return;
	}

	if (g_loader.Load(path.c_str())) {
		char dbg[512];
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] LoadDll OK: %s\n", path.c_str());
		OutputDebugStringA(dbg);
		g_pipe.SendResponse(kRespOk);
	} else {
		char dbg[512];
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] LoadDll FAILED: %s (error %lu)\n",
			path.c_str(), GetLastError());
		OutputDebugStringA(dbg);
		g_pipe.SendResponse(kRespError);
	}
}

static void HandleInitMachine() {
	OutputDebugStringA("[BuzzBridgeHost32] HandleInitMachine entered\n");
	g_loader.UpdateMasterInfo(125.0, 44100.0);

	{
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] About to call InitMachine: IsLoaded=%d IsFaulted=%d Info=%p\n",
			(int)g_loader.IsLoaded(), (int)g_loader.IsFaulted(),
			(void*)g_loader.GetInfo());
		OutputDebugStringA(dbg);
	}

	// Direct inline test: log before and after each step to find the failure
	OutputDebugStringA("[BuzzBridgeHost32] Step 1: checking preconditions\n");
	{
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] hDll=%p pInfo=%p\n",
			(void*)g_loader.GetInfo(), (void*)g_loader.GetInfo());
		OutputDebugStringA(dbg);
	}

	bool initOk = false;
	__try {
		OutputDebugStringA("[BuzzBridgeHost32] Step 2: calling g_loader.InitMachine()\n");
		initOk = g_loader.InitMachine();
		char dbg[64];
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] Step 3: InitMachine returned %d\n", (int)initOk);
		OutputDebugStringA(dbg);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		char dbg[128];
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] InitMachine CRASHED with exception 0x%08lX\n",
			GetExceptionCode());
		OutputDebugStringA(dbg);
	}

	if (initOk) {
		auto* info = g_loader.GetInfo();
		auto* machine = g_loader.GetMachine();
		g_numTracks = (info && info->minTracks > 0) ? info->minTracks : 1;
		g_firstTick = true;
		g_workCrashCount = 0;
		{
			char dbg[256];
			snprintf(dbg, sizeof(dbg),
				"[BuzzBridgeHost32] InitMachine OK: GlobalVals=%p TrackVals=%p numTracks=%d\n",
				machine ? machine->GlobalVals : nullptr,
				machine ? machine->TrackVals : nullptr,
				g_numTracks);
			OutputDebugStringA(dbg);
		}

		// Write defaults to parameter buffers AFTER Init+SetNumTracks.
		// Skip if machine is faulted (SetNumTracks crashed).
		if (!g_loader.IsFaulted()) {
			auto* layout = g_loader.GetParamLayout();
			if (layout) {
				layout->WriteAllDefaults(machine->GlobalVals);

				auto& tSlots = layout->GetTrackSlots();
				if (machine->TrackVals && !tSlots.empty()) {
					for (int t = 0; t < g_numTracks; t++) {
						for (int i = 0; i < (int)tSlots.size(); i++) {
							layout->WriteTrackParam(machine->TrackVals, t, i, tSlots[i].param->DefValue);
						}
					}
				}
			}
			OutputDebugStringA("[BuzzBridgeHost32] Parameter defaults written\n");
		} else {
			OutputDebugStringA("[BuzzBridgeHost32] Skipping defaults (machine faulted)\n");
		}

		g_pipe.SendResponse(kRespOk);
	} else {
		OutputDebugStringA("[BuzzBridgeHost32] InitMachine FAILED\n");
		g_pipe.SendResponse(kRespError);
	}
}

static void HandleUnload() {
	auto* cb = g_loader.GetCallbacks();
	// Clear MDK state. The machine's destructor may try to call the MDK
	// destructor at offset 0x18, but our stub's dtor is a no-op.
	// We need to null out offset 0x18 on the machine BEFORE Unload destroys it,
	// so the machine's destructor doesn't call our freed stub.
	if (cb->isMDKMachine && cb->mdkStub) {
		auto* machine = g_loader.GetMachine();
		if (machine) {
			int** m = (int**)machine;
			m[6] = nullptr; // clear offset 0x18 so machine dtor doesn't use freed MDK
		}
	}
	if (cb->mdkStub) {
		DestroyMDKStub(cb->mdkStub);
		cb->mdkStub = nullptr;
	}
	cb->isMDKMachine = false;
	g_loader.Unload();
	g_pipe.SendResponse(kRespOk);
}

static void HandleSetMasterInfo() {
	BridgeMasterInfo mi;
	if (!g_pipe.ReadAll(&mi, sizeof(mi))) {
		g_pipe.SendResponse(kRespError);
		return;
	}
	g_loader.UpdateMasterInfo(mi.beatsPerMin, mi.samplesPerSec, mi.ticksPerBeat);
	g_pipe.SendResponse(kRespOk);
}

static void HandleTick(uint32_t payloadSize) {
	// Read param changes
	uint32_t numParams = payloadSize / sizeof(BridgeTickParam);
	std::vector<BridgeTickParam> params(numParams);
	if (numParams > 0) {
		if (!g_pipe.ReadAll(params.data(), payloadSize)) {
			g_pipe.SendResponse(kRespError);
			return;
		}
	}

	auto* machine = g_loader.GetMachine();
	auto* layout = g_loader.GetParamLayout();
	if (!machine || !layout || g_loader.IsFaulted()) {
		g_pipe.SendResponse(kRespError);
		return;
	}

	auto& gSlots = layout->GetGlobalSlots();
	auto& tSlots = layout->GetTrackSlots();
	int numTrackParams = (int)tSlots.size();
	int numTracks = g_numTracks;

	// On the first tick after init, let the machine use its constructor defaults
	// UNLESS the processor sent explicit param values (state restore).
	// Writing params overwrites the machine's internal state, which kills audio
	// on many machines when loading fresh. But after a DAW reload, we need to
	// restore saved values.
	{
		bool hasExplicitParams = false;
		for (auto& p : params) {
			if (p.paramId < 0) break; // sentinel
			hasExplicitParams = true;
			break;
		}
		if (g_firstTick && !hasExplicitParams) {
			OutputDebugStringA("[BuzzBridgeHost32] First tick: skipping param writes (no explicit params)\n");
			goto do_tick;
		}
		if (g_firstTick && hasExplicitParams) {
			OutputDebugStringA("[BuzzBridgeHost32] First tick: applying explicit params (state restore)\n");
		}
	}

	// Reset all params to NoValue before applying changes (standard Buzz convention).
	if (machine->GlobalVals)
		layout->WriteAllNoValues(machine->GlobalVals);
	if (machine->TrackVals && numTrackParams > 0)
		layout->WriteTrackAllNoValues(machine->TrackVals, numTracks);

	// Apply changed params.
	{
		static int tickLog = 0;
		for (auto& p : params) {
			if (p.paramId < 0) break; // sentinel
			if (tickLog < 10) {
				char dbg[128];
				snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] Tick param: id=%d value=%d\n",
					p.paramId, p.value);
				OutputDebugStringA(dbg);
			}
			// Always log note-on events (track param 0 with value != 0 and != 255)
			if (p.paramId == 1000 && p.value != 0 && p.value != 255) {
				char dbg[128];
				snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] NOTE ON tick: paramId=%d buzzNote=%d\n",
					p.paramId, p.value);
				OutputDebugStringA(dbg);

				// Dump TrackVals bytes with note
				if (machine->TrackVals) {
					int structSize = layout->GetTrackStructSize();
					unsigned char* tv = (unsigned char*)machine->TrackVals;
					char hex[256] = {};
					int pos = 0;
					for (int b = 0; b < structSize && pos < 200; b++) {
						pos += snprintf(hex + pos, 256 - pos, "%02X ", tv[b]);
					}
					char dbg2[512];
					snprintf(dbg2, sizeof(dbg2),
						"[BuzzBridgeHost32] TrackVals after note write: %s\n", hex);
					OutputDebugStringA(dbg2);
				}
			}
			if (p.paramId < 1000) {
				// Global param
				if (p.paramId >= 0 && p.paramId < (int)gSlots.size() && machine->GlobalVals)
					layout->WriteGlobalParam(machine->GlobalVals, p.paramId, p.value);
			} else {
				// Track param: encoded as 1000 + track * numTrackParams + paramIndex
				int encoded = p.paramId - 1000;
				int track = (numTrackParams > 0) ? encoded / numTrackParams : 0;
				int paramIdx = (numTrackParams > 0) ? encoded % numTrackParams : 0;
				if (track >= 0 && track < numTracks &&
				    paramIdx >= 0 && paramIdx < numTrackParams && machine->TrackVals) {
					layout->WriteTrackParam(machine->TrackVals, track, paramIdx, p.value);
					if (tickLog < 10) {
						char dbg[128];
						snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] -> track=%d param=%d value=%d\n",
							track, paramIdx, p.value);
						OutputDebugStringA(dbg);
					}
				}
			}
		}
		tickLog++;
	}

	// Log master info and memory layout state
	{
		static int miLog = 0;
		if (miLog++ < 3) {
			auto* mi = g_loader.GetMasterInfo();
			char dbg[512];
			snprintf(dbg, sizeof(dbg),
				"[BuzzBridgeHost32] Tick: MasterInfo BPM=%d SPS=%d SPT=%d TPB=%d PosInTick=%d "
				"GlobalVals=%p TrackVals=%p gSlots=%d tSlots=%d trackStructSize=%d\n",
				mi ? mi->BeatsPerMin : -1, mi ? mi->SamplesPerSec : -1,
				mi ? mi->SamplesPerTick : -1, mi ? mi->TicksPerBeat : -1,
				mi ? mi->PosInTick : -1,
				machine->GlobalVals, machine->TrackVals,
				(int)gSlots.size(), (int)tSlots.size(),
				layout->GetTrackStructSize());
			OutputDebugStringA(dbg);
		}
	}

do_tick:
	// Reset PosInTick for the new tick
	auto* mi = g_loader.GetMasterInfo();
	if (mi) mi->PosInTick = 0;

	// Dump TrackVals bytes right before Tick (after all params applied)
	{
		static int dumpLog = 0;
		// Only dump when a note-on is present (to avoid spam)
		if (dumpLog < 5 && machine->TrackVals && numTrackParams > 0) {
			// Check if there's a note param in this tick
			bool hasNote = false;
			for (auto& p : params) {
				if (p.paramId < 0) break;
				if (p.paramId == 1000 && p.value != 0 && p.value != 255) { hasNote = true; break; }
			}
			if (hasNote) {
				int structSize = layout->GetTrackStructSize();
				unsigned char* tv = (unsigned char*)machine->TrackVals;
				char hex[256] = {};
				int pos = 0;
				for (int b = 0; b < structSize && pos < 200; b++) {
					pos += snprintf(hex + pos, 256 - pos, "%02X ", tv[b]);
				}
				char dbg[512];
				snprintf(dbg, sizeof(dbg),
					"[BuzzBridgeHost32] TrackVals FINAL before Tick (size=%d): %s\n", structSize, hex);
				OutputDebugStringA(dbg);
				dumpLog++;
			}
		}
	}

	// Call Tick
	bool ok = SEH_Call([&]() { machine->Tick(); });
	if (!ok) g_workCrashCount++;
	g_firstTick = false;
	g_pipe.SendResponse(ok ? kRespOk : kRespError);
}

// Process Work using params already in shared memory (fast path)
static void DoWork(int numSamples, int workMode) {
	auto* machine = g_loader.GetMachine();
	auto* info = g_loader.GetInfo();
	if (!machine || !info || g_loader.IsFaulted()) return;
	if (g_workCrashCount > 3) return; // stop calling after repeated crashes

	auto* audio = g_sharedMem.GetAudio();
	if (!audio) return;

	if (numSamples <= 0 || numSamples > kBridgeMaxSamples) return;

	bool monoToStereo = (info->Flags & MIF_MONO_TO_STEREO) != 0;
	bool hasOutput = false;
	auto* mi = g_loader.GetMasterInfo();

	int offset = 0;
	while (offset < numSamples) {
		int blockSize = numSamples - offset;
		if (blockSize > MAX_BUFFER_LENGTH) blockSize = MAX_BUFFER_LENGTH;

		if (g_loader.GetCallbacks()->isMDKMachine) {
			// MDK machines expect stereo interleaved: [L0, R0, L1, R1, ...]
			// Oversized to 4x + slop because some MDK stereo effects write
			// extra beyond what we pass as numsamples — same protection as
			// the standalone test harness.
			float interleavedBuf[MAX_BUFFER_LENGTH * 4 + 16] = {};
			if (workMode & WM_READ) {
				for (int i = 0; i < blockSize; i++) {
					interleavedBuf[i * 2] = audio->inputLeft[offset + i];
					interleavedBuf[i * 2 + 1] = audio->inputRight[offset + i];
				}
			} else {
				memset(interleavedBuf, 0, blockSize * 2 * sizeof(float));
			}
			bool blockOut = false;
			bool ok = SEH_Call([&]() {
				blockOut = machine->Work(interleavedBuf, blockSize, workMode);
			});
			if (!ok) {
				g_workCrashCount++;
				static int mdkCrashLog = 0;
				if (mdkCrashLog++ < 10) {
					char dbg[256];
					snprintf(dbg, sizeof(dbg),
						"[BuzzBridgeHost32] MDK Work CRASH: count=%d blk=%d mode=%d pos30=%d pos34=%d\n",
						g_workCrashCount, blockSize, workMode,
						*(int*)((char*)machine + 0x30), *(int*)((char*)machine + 0x34));
					OutputDebugStringA(dbg);
				}
				return;
			}
			if (blockOut) {
				// De-interleave stereo output
				for (int i = 0; i < blockSize; i++) {
					audio->outputLeft[offset + i] = interleavedBuf[i * 2];
					audio->outputRight[offset + i] = interleavedBuf[i * 2 + 1];
				}
				hasOutput = true;
			} else {
				memset(audio->outputLeft + offset, 0, blockSize * sizeof(float));
				memset(audio->outputRight + offset, 0, blockSize * sizeof(float));
			}
		} else if (monoToStereo) {
			// Oversized: some mono-to-stereo machines interpret pout as
			// interleaved stereo and write 2*numsamples floats there. A
			// stack-sized [MAX_BUFFER_LENGTH] outBuf overflows the stack
			// frame in that case. This was the cause of Ruff SPECCY II
			// and similar MDK-derived generators crashing BespokeSynth
			// with /GS cookie violations (STATUS_STACK_BUFFER_OVERRUN).
			float inBuf[MAX_BUFFER_LENGTH * 2 + 16]  = {};
			float outBuf[MAX_BUFFER_LENGTH * 2 + 16] = {};
			if (workMode & WM_READ) {
				memcpy(inBuf, audio->inputLeft + offset, blockSize * sizeof(float));
			}
			bool blockOut = false;
			bool ok = SEH_Call([&]() {
				blockOut = machine->WorkMonoToStereo(inBuf, outBuf, blockSize, workMode);
			});
			if (!ok) { g_workCrashCount++; return; }
			if (blockOut) {
				memcpy(audio->outputLeft + offset, inBuf, blockSize * sizeof(float));
				memcpy(audio->outputRight + offset, outBuf, blockSize * sizeof(float));
				hasOutput = true;
			} else {
				memset(audio->outputLeft + offset, 0, blockSize * sizeof(float));
				memset(audio->outputRight + offset, 0, blockSize * sizeof(float));
			}
		} else {
			// Oversized: some generators and stereo effects write past
			// the nominal numsamples end of psamples. See the comment in
			// the MonoToStereo branch and in test_AllMachines.cpp.
			float workBuf[MAX_BUFFER_LENGTH * 2 + 16] = {};
			if (workMode & WM_READ) {
				memcpy(workBuf, audio->inputLeft + offset, blockSize * sizeof(float));
			}
			bool blockOut = false;
			bool ok = SEH_Call([&]() {
				blockOut = machine->Work(workBuf, blockSize, workMode);
			});
			if (!ok) { g_workCrashCount++; return; }
			if (blockOut) {
				memcpy(audio->outputLeft + offset, workBuf, blockSize * sizeof(float));
				memcpy(audio->outputRight + offset, workBuf, blockSize * sizeof(float));
				hasOutput = true;
			} else {
				memset(audio->outputLeft + offset, 0, blockSize * sizeof(float));
				memset(audio->outputRight + offset, 0, blockSize * sizeof(float));
			}
		}
		offset += blockSize;
		if (mi) mi->PosInTick += blockSize;
	}
	audio->hasOutput = hasOutput ? 1 : 0;
}

// Fast Work thread: waits on event, processes Work via shared memory
static DWORD WINAPI FastWorkThread(LPVOID) {
	while (g_running) {
		DWORD result = WaitForSingleObject(g_hWorkReady, 100);
		if (result == WAIT_OBJECT_0) {
			auto* audio = g_sharedMem.GetAudio();
			if (audio) {
				DoWork(audio->numSamples, audio->workMode);
			}
			SetEvent(g_hWorkDone);
		}
	}
	return 0;
}

static void HandleWork() {
	BridgeWorkCmd wcmd;
	if (!g_pipe.ReadAll(&wcmd, sizeof(wcmd))) {
		g_pipe.SendResponse(kRespError);
		return;
	}
	DoWork(wcmd.numSamples, wcmd.workMode);
	g_pipe.SendResponse(kRespOk);
}

static void HandleStop() {
	g_loader.StopMachine();
	g_pipe.SendResponse(kRespOk);
}

static void HandleSetNumTracks() {
	int32_t n = 0;
	if (!g_pipe.ReadAll(&n, sizeof(n))) {
		g_pipe.SendResponse(kRespError);
		return;
	}
	auto* info = g_loader.GetInfo();
	int minT = (info && info->minTracks > 0) ? info->minTracks : 1;
	int maxT = (info && info->maxTracks > 0) ? info->maxTracks : 1;
	if (n < minT) n = minT;
	if (n > maxT) n = maxT;
	g_numTracks = n;

	auto* machine = g_loader.GetMachine();
	if (machine) {
		SEH_Call([&]() { machine->SetNumTracks(n); });
	}
	g_pipe.SendResponse(kRespOk);
}

static void HandleDescribeValue() {
	BridgeDescribeValue dv;
	if (!g_pipe.ReadAll(&dv, sizeof(dv))) {
		g_pipe.SendResponse(kRespError);
		return;
	}

	auto* machine = g_loader.GetMachine();
	if (!machine) {
		g_pipe.SendResponse(kRespError);
		return;
	}

	const char* desc = nullptr;
	SEH_Call([&]() {
		desc = machine->DescribeValue(dv.param, dv.value);
	});

	if (desc && desc[0]) {
		// Safely measure length — machine may return a bad pointer
		uint32_t len = 0;
		__try {
			len = (uint32_t)strnlen(desc, 255);
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
			len = 0;
		}
		if (len > 0) {
			g_pipe.SendResponse(kRespDescribeValue, desc, len);
		} else {
			g_pipe.SendResponse(kRespDescribeValue, "", 0);
		}
	} else {
		g_pipe.SendResponse(kRespDescribeValue, "", 0);
	}
}

static void HandleMidiNote() {
	BridgeMidiNote mn;
	if (!g_pipe.ReadAll(&mn, sizeof(mn))) {
		g_pipe.SendResponse(kRespError);
		return;
	}

	auto* machine = g_loader.GetMachine();
	if (machine) {
		SEH_Call([&]() { machine->MidiNote(mn.channel, mn.note, mn.velocity); });
	}
	g_pipe.SendResponse(kRespOk);
}

static void HandleMidiCC() {
	BridgeMidiCC cc;
	if (!g_pipe.ReadAll(&cc, sizeof(cc))) {
		g_pipe.SendResponse(kRespError);
		return;
	}

	auto* machineEx = g_loader.GetMachineEx();
	if (machineEx) {
		SEH_Call([&]() { machineEx->MidiControlChange(cc.controller, cc.channel, cc.value); });
	}
	g_pipe.SendResponse(kRespOk);
}

static void HandleLoadWave(uint32_t payloadSize) {
	BridgeLoadWave lw;
	if (payloadSize < sizeof(lw) || !g_pipe.ReadAll(&lw, sizeof(lw))) {
		g_pipe.SendResponse(kRespError);
		return;
	}

	std::string path;
	if (lw.pathLen > 0) {
		uint32_t remaining = payloadSize - sizeof(lw);
		uint32_t toRead = std::min((uint32_t)lw.pathLen, remaining);
		std::vector<char> buf(toRead + 1, 0);
		if (!g_pipe.ReadAll(buf.data(), toRead)) {
			g_pipe.SendResponse(kRespError);
			return;
		}
		path = buf.data();
	}

	auto* waveTable = g_loader.GetWaveTable();
	bool ok = false;
	if (waveTable && !path.empty()) {
		if (lw.slotIndex > 0) {
			ok = waveTable->LoadWav(lw.slotIndex, path);
		} else {
			ok = waveTable->LoadWavAuto(path) > 0;
		}
	}
	g_pipe.SendResponse(ok ? kRespOk : kRespError);
}

static void HandleClearWaves() {
	auto* waveTable = g_loader.GetWaveTable();
	if (waveTable) waveTable->ClearAll();
	g_pipe.SendResponse(kRespOk);
}

static void HandleGetInfo() {
	auto* info = g_loader.GetInfo();
	if (!info) {
		g_pipe.SendResponse(kRespError);
		return;
	}

	auto* layout = g_loader.GetParamLayout();
	auto& gSlots = layout->GetGlobalSlots();
	auto& tSlots = layout->GetTrackSlots();

	BridgeMachineInfo bmi = {};
	bmi.type = info->Type;
	bmi.flags = info->Flags;
	bmi.numGlobalParams = (int32_t)gSlots.size();
	bmi.numTrackParams = (int32_t)tSlots.size();
	bmi.minTracks = info->minTracks;
	bmi.maxTracks = info->maxTracks;
	// Copy strings from machine info with SEH protection (pointers may be garbage)
	SEH_Call([&]() {
		if (info->Name) {
			strncpy(bmi.name, info->Name, sizeof(bmi.name) - 1);
			bmi.name[sizeof(bmi.name) - 1] = '\0';
		}
		if (info->ShortName) {
			strncpy(bmi.shortName, info->ShortName, sizeof(bmi.shortName) - 1);
			bmi.shortName[sizeof(bmi.shortName) - 1] = '\0';
		}
		if (info->Author) {
			strncpy(bmi.author, info->Author, sizeof(bmi.author) - 1);
			bmi.author[sizeof(bmi.author) - 1] = '\0';
		}
	});

	// Build param info array
	int totalParams = bmi.numGlobalParams + bmi.numTrackParams;
	std::vector<BridgeParamInfo> paramInfos(totalParams);

	for (int i = 0; i < bmi.numGlobalParams; i++) {
		auto* bp = gSlots[i].param;
		auto& pi = paramInfos[i];
		pi.type = bp->Type;
		pi.minValue = bp->MinValue;
		pi.maxValue = bp->MaxValue;
		pi.noValue = bp->NoValue;
		pi.defValue = bp->DefValue;
		pi.flags = bp->Flags;
		if (bp->Name) {
			strncpy(pi.name, bp->Name, sizeof(pi.name) - 1);
			pi.name[sizeof(pi.name) - 1] = '\0';
		}
		if (bp->Description) {
			strncpy(pi.description, bp->Description, sizeof(pi.description) - 1);
			pi.description[sizeof(pi.description) - 1] = '\0';
		}
	}

	for (int i = 0; i < bmi.numTrackParams; i++) {
		auto* bp = tSlots[i].param;
		auto& pi = paramInfos[bmi.numGlobalParams + i];
		pi.type = bp->Type;
		pi.minValue = bp->MinValue;
		pi.maxValue = bp->MaxValue;
		pi.noValue = bp->NoValue;
		pi.defValue = bp->DefValue;
		pi.flags = bp->Flags;
		if (bp->Name) {
			strncpy(pi.name, bp->Name, sizeof(pi.name) - 1);
			pi.name[sizeof(pi.name) - 1] = '\0';
		}
		if (bp->Description) {
			strncpy(pi.description, bp->Description, sizeof(pi.description) - 1);
			pi.description[sizeof(pi.description) - 1] = '\0';
		}
	}

	uint32_t payloadSize = sizeof(bmi) + totalParams * sizeof(BridgeParamInfo);
	BridgeRespHeader hdr = { kRespMachineInfo, payloadSize };
	g_pipe.WriteAll(&hdr, sizeof(hdr));
	g_pipe.WriteAll(&bmi, sizeof(bmi));
	if (totalParams > 0) {
		g_pipe.WriteAll(paramInfos.data(), totalParams * sizeof(BridgeParamInfo));
	}
}

static void HandleSetParam() {
	BridgeParamValue pv;
	if (!g_pipe.ReadAll(&pv, sizeof(pv))) {
		g_pipe.SendResponse(kRespError);
		return;
	}
	// Param values are applied at Tick time via HandleTick
	// This is a placeholder for immediate param set if needed
	g_pipe.SendResponse(kRespOk);
}

static void CommandLoop() {
	while (g_running) {
		BridgeCmdHeader cmd;
		if (!g_pipe.ReadCommand(cmd)) {
			// Pipe broken - exit
			break;
		}

		// Only log non-hot-path commands (skip Tick/Work/SetMasterInfo)
		if (cmd.cmd != kCmdTick && cmd.cmd != kCmdWork && cmd.cmd != kCmdSetMasterInfo) {
			char dbg[128];
			snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] Cmd: %u payload: %u\n",
				(unsigned)cmd.cmd, (unsigned)cmd.payloadSize);
			OutputDebugStringA(dbg);
		}

		switch (cmd.cmd) {
			case kCmdPing:
				g_pipe.SendResponse(kRespOk);
				break;
			case kCmdLoadDll:
				HandleLoadDll(cmd.payloadSize);
				break;
			case kCmdInitMachine:
				HandleInitMachine();
				break;
			case kCmdUnload:
				HandleUnload();
				break;
			case kCmdSetMasterInfo:
				HandleSetMasterInfo();
				break;
			case kCmdTick:
				HandleTick(cmd.payloadSize);
				break;
			case kCmdWork:
				HandleWork();
				break;
			case kCmdStop:
				HandleStop();
				break;
			case kCmdMidiNote:
				HandleMidiNote();
				break;
			case kCmdMidiCC:
				HandleMidiCC();
				break;
			case kCmdGetInfo:
				HandleGetInfo();
				break;
			case kCmdSetParam:
				HandleSetParam();
				break;
			case kCmdLoadWave:
				HandleLoadWave(cmd.payloadSize);
				break;
			case kCmdClearWaves:
				HandleClearWaves();
				break;
			case kCmdSetNumTracks:
				HandleSetNumTracks();
				break;
			case kCmdDescribeValue:
				HandleDescribeValue();
				break;
			case kCmdShutdown:
				g_pipe.SendResponse(kRespOk);
				g_running = false;
				break;
			default: {
				// Skip unknown payload
				if (cmd.payloadSize > 0) {
					std::vector<char> skip(cmd.payloadSize);
					g_pipe.ReadAll(skip.data(), cmd.payloadSize);
				}
				g_pipe.SendResponse(kRespError);
				break;
			}
		}
	}
}

int main(int argc, char* argv[])
{
	// Patch MessageBox before loading any machine DLLs
	PatchMessageBoxes();

	{
		unsigned int fpuCw = _controlfp(0, 0);
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] FPU control word: 0x%08X\n", fpuCw);
		OutputDebugStringA(dbg);
	}

	// Standalone test mode: BuzzBridgeHost32.exe --test <dll_path>
	if (argc >= 3 && strcmp(argv[1], "--test") == 0) {
		setvbuf(stdout, NULL, _IONBF, 0);
		setvbuf(stderr, NULL, _IONBF, 0);
		printf("FPU: 0x%08X\n", _controlfp(0, 0));
		printf("Testing: %s\n", argv[2]);

		BuzzMachineLoader loader;
		printf("Loading DLL...\n");
		if (!loader.Load(argv[2])) {
			printf("Load FAILED (error %lu)\n", GetLastError());
			return 1;
		}
		auto* info = loader.GetInfo();
		printf("Load OK: '%s' type=%d gParams=%d tParams=%d minTracks=%d maxTracks=%d attrs=%d\n",
			info->Name ? info->Name : "(null)",
			info->Type, info->numGlobalParameters, info->numTrackParameters,
			info->minTracks, info->maxTracks, info->numAttributes);

		// Print all parameters
		int totalParams = info->numGlobalParameters + info->numTrackParameters;
		for (int i = 0; i < totalParams; i++) {
			auto* p = info->Parameters[i];
			const char* group = (i < info->numGlobalParameters) ? "G" : "T";
			int idx = (i < info->numGlobalParameters) ? i : i - info->numGlobalParameters;
			printf("  %s[%d] '%s': type=%d min=%d max=%d noVal=%d def=%d flags=0x%x\n",
				group, idx, p->Name ? p->Name : "?",
				p->Type, p->MinValue, p->MaxValue, p->NoValue, p->DefValue, p->Flags);
		}

		loader.UpdateMasterInfo(125.0, 44100.0);
		printf("Calling InitMachine...\n");
		if (!loader.InitMachine()) {
			printf("InitMachine FAILED (IsFaulted=%d)\n", (int)loader.IsFaulted());
			return 1;
		}
		printf("InitMachine OK\n");
		printf("isMDKMachine: %d\n", (int)loader.GetCallbacks()->isMDKMachine);
		auto* m = loader.GetMachine();
		auto* l = loader.GetParamLayout();

		// Write defaults (like the bridge host does)
		l->WriteAllDefaults(m->GlobalVals);
		auto& ts = l->GetTrackSlots();
		if (m->TrackVals && !ts.empty()) {
			int nTracks = (loader.GetInfo()->minTracks > 0) ? loader.GetInfo()->minTracks : 1;
			for (int t = 0; t < nTracks; t++) {
				for (int i = 0; i < (int)ts.size(); i++) {
					l->WriteTrackParam(m->TrackVals, t, i, ts[i].param->DefValue);
				}
			}
		}

		SEH_Call([&]() { m->Tick(); });

		// Second tick: write a note trigger
		for (int i = 0; i < (int)ts.size(); i++) {
			if (ts[i].param->Type == pt_note) {
				l->WriteTrackParam(m->TrackVals, 0, i, 0x51); // C-5
				if (i + 1 < (int)ts.size())
					l->WriteTrackParam(m->TrackVals, 0, i + 1, ts[i+1].param->MaxValue);
				break;
			}
		}
		SEH_Call([&]() { m->Tick(); });

		bool isMDK = loader.GetCallbacks()->isMDKMachine;
		printf("isMDKMachine=%d\n", (int)isMDK);

		if (isMDK) {
			// MDK machines expect stereo interleaved audio: [L0, R0, L1, R1, ...]
			// Feed a test signal (sine wave) and call Work multiple times to fill
			// the overlap-add window (typically 2048 samples).
			const int blockSize = 256;
			const int numBlocks = 16; // 16 * 256 = 4096 > 2048 window
			float interleavedBuf[blockSize * 2];
			float mx = 0;
			bool out = false;

			for (int blk = 0; blk < numBlocks; blk++) {
				// Generate stereo test signal (sine at ~440Hz)
				for (int i = 0; i < blockSize; i++) {
					float t = (float)(blk * blockSize + i) / 44100.0f;
					float sample = 0.5f * sinf(2.0f * 3.14159265f * 440.0f * t);
					interleavedBuf[i * 2]     = sample; // left
					interleavedBuf[i * 2 + 1] = sample; // right
				}
				bool blockOut = false;
				bool workOk = SEH_Call([&]() { blockOut = m->Work(interleavedBuf, blockSize, WM_READWRITE); });
				if (!workOk) {
					printf("Work CRASHED on block %d\n", blk);
					break;
				}
				if (blockOut) {
					out = true;
					for (int i = 0; i < blockSize * 2; i++) {
						float a = interleavedBuf[i] < 0 ? -interleavedBuf[i] : interleavedBuf[i];
						if (a > mx) mx = a;
					}
				}
			}
			printf("hasOutput=%d max=%f\n", (int)out, mx);
			printf(mx > 0 ? "*** AUDIO OK ***\n" : "*** NO AUDIO ***\n");
		} else {
			float buf[256] = {};
			bool out = false;
			SEH_Call([&]() { out = m->Work(buf, 256, WM_WRITE); });

			float mx = 0;
			for (int i = 0; i < 256; i++) {
				float a = buf[i] < 0 ? -buf[i] : buf[i];
				if (a > mx) mx = a;
			}
			printf("hasOutput=%d max=%f buf[0]=%f\n", (int)out, mx, buf[0]);
			printf(mx > 0 ? "*** AUDIO OK ***\n" : "*** NO AUDIO ***\n");
		}

		printf("Test complete, cleaning up...\n");
		// Clean up MDK before machine destruction to avoid heap issues
		auto* cb = loader.GetCallbacks();
		if (cb->isMDKMachine && cb->mdkStub) {
			int** machineSlots = (int**)m;
			machineSlots[6] = nullptr; // clear offset 0x18 before machine dtor
		}
		printf("Unloading...\n");
		return 0;
	}

	OutputDebugStringA("[BuzzBridgeHost32] Starting...\n");

	if (argc < 2) {
		OutputDebugStringA("[BuzzBridgeHost32] ERROR: No session ID argument\n");
		return 1;
	}

	g_sessionId = argv[1];

	char dbg[512];
	snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] Session: %s\n", g_sessionId.c_str());
	OutputDebugStringA(dbg);

	// Create named pipe (server end)
	std::string pipeName = BridgePipeName(g_sessionId);
	snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] Creating pipe: %s\n", pipeName.c_str());
	OutputDebugStringA(dbg);

	HANDLE hPipe = CreateNamedPipeA(
		pipeName.c_str(),
		PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1, // max instances
		65536, 65536,
		5000, // timeout ms
		nullptr
	);

	if (hPipe == INVALID_HANDLE_VALUE) {
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] Failed to create pipe (error %lu)\n", GetLastError());
		OutputDebugStringA(dbg);
		return 1;
	}

	OutputDebugStringA("[BuzzBridgeHost32] Pipe created, waiting for connection...\n");
	g_pipe.SetHandle(hPipe);

	// Create shared memory for audio
	std::string shmName = BridgeSharedMemName(g_sessionId);
	if (!g_sharedMem.Create(shmName, sizeof(BridgeSharedAudio))) {
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] Failed to create shared memory: %s (error %lu)\n",
			shmName.c_str(), GetLastError());
		OutputDebugStringA(dbg);
		return 1;
	}

	// Create events for fast Work path
	std::string workReadyName = "Local\\BuzzBridgeWorkReady_" + g_sessionId;
	std::string workDoneName = "Local\\BuzzBridgeWorkDone_" + g_sessionId;
	g_hWorkReady = CreateEventA(nullptr, FALSE, FALSE, workReadyName.c_str()); // auto-reset
	g_hWorkDone = CreateEventA(nullptr, FALSE, FALSE, workDoneName.c_str());   // auto-reset

	OutputDebugStringA("[BuzzBridgeHost32] Shared memory created, waiting for client...\n");

	// Wait for the 64-bit plugin to connect
	if (!ConnectNamedPipe(hPipe, nullptr)) {
		DWORD err = GetLastError();
		if (err != ERROR_PIPE_CONNECTED) {
			snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] ConnectNamedPipe failed (error %lu)\n", err);
			OutputDebugStringA(dbg);
			return 1;
		}
	}

	OutputDebugStringA("[BuzzBridgeHost32] Client connected, entering command loop\n");

	// Start fast Work thread (event-based, no pipe overhead)
	HANDLE hFastWorkThread = nullptr;
	if (g_hWorkReady && g_hWorkDone) {
		hFastWorkThread = CreateThread(nullptr, 0, FastWorkThread, nullptr, 0, nullptr);
		if (hFastWorkThread) {
			OutputDebugStringA("[BuzzBridgeHost32] Fast Work thread started\n");
		}
	}

	// Enter command loop
	CommandLoop();

	// Stop fast Work thread
	g_running = false;
	if (hFastWorkThread) {
		WaitForSingleObject(hFastWorkThread, 2000);
		CloseHandle(hFastWorkThread);
	}

	// Cleanup
	g_loader.Unload();
	g_pipe.Close();
	g_sharedMem.Close();
	if (g_hWorkReady) { CloseHandle(g_hWorkReady); g_hWorkReady = nullptr; }
	if (g_hWorkDone) { CloseHandle(g_hWorkDone); g_hWorkDone = nullptr; }

	return 0;
}
