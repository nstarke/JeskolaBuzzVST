#include <windows.h>
#include "BuzzMachineLoader.h"
#include "BuzzMachineQuirks.h"
#include "../common/SEHGuard.h"
#include "../common/PatchMessageBoxes.h"
#include <cstring>
#include <cstdio>

namespace BuzzVst {

BuzzMachineLoader::BuzzMachineLoader()
{
	memset(&masterInfo, 0, sizeof(masterInfo));
	masterInfo.BeatsPerMin = 125;
	masterInfo.TicksPerBeat = 4;
	masterInfo.SamplesPerSec = 44100;
	masterInfo.SamplesPerTick = (60 * 44100) / (125 * 4);
	masterInfo.PosInTick = 0;
	masterInfo.TicksPerSec = (float)44100 / (float)masterInfo.SamplesPerTick;
	masterInfo.GrooveSize = 0;
	masterInfo.PosInGroove = 0;
	masterInfo.GrooveData = nullptr;

	callbacks.masterInfoPtr = &masterInfo;
	callbacks.waveTable = &waveTable;
}

BuzzMachineLoader::~BuzzMachineLoader()
{
	Unload();
}

bool BuzzMachineLoader::ValidateInfo(const CMachineInfo* info) const
{
	if (!info) return false;

	// Validate type
	if (info->Type != MT_GENERATOR && info->Type != MT_EFFECT)
		return false;

	// Validate parameter count
	if (info->numGlobalParameters < 0 || info->numGlobalParameters > 256)
		return false;
	if (info->numTrackParameters < 0 || info->numTrackParameters > 256)
		return false;

	// Validate parameter pointers if count > 0
	int totalParams = info->numGlobalParameters + info->numTrackParameters;
	if (totalParams > 0 && !info->Parameters)
		return false;

	// Spot-check ALL parameter pointers under SEH
	if (totalParams > 0) {
		BUZZ_SEH_TRY {
			for (int i = 0; i < totalParams; i++) {
				if (!info->Parameters[i]) return false;
				volatile int t = info->Parameters[i]->Type;
				volatile const char* name = info->Parameters[i]->Name;
				(void)t; (void)name;
			}
		}
		BUZZ_SEH_EXCEPT {
			return false;
		}
	}

	// Validate track counts
	if (info->minTracks < 0 || info->maxTracks < 0)
		return false;
	if (info->maxTracks < info->minTracks)
		return false;

	// Validate attribute count and pointers
	if (info->numAttributes < 0 || info->numAttributes > 256)
		return false;
	if (info->numAttributes > 0) {
		if (!info->Attributes) return false;
		BUZZ_SEH_TRY {
			for (int i = 0; i < info->numAttributes; i++) {
				if (!info->Attributes[i]) return false;
				volatile int d = info->Attributes[i]->DefValue;
				(void)d;
			}
		}
		BUZZ_SEH_EXCEPT {
			return false;
		}
	}

	return true;
}

bool BuzzMachineLoader::Load(const char* dllPath)
{
	// Suppress machine MessageBox popups before LoadLibrary runs their DllMain.
	PatchMessageBoxesOnce();

	Unload();
	faulted = false;

	if (!dllPath || !dllPath[0])
		return false;

	// Set DLL search path to the machine's directory so dependencies can be found
	{
		std::string dir(dllPath);
		auto pos = dir.find_last_of("\\/");
		if (pos != std::string::npos) {
			dir = dir.substr(0, pos);
			SetDllDirectoryA(dir.c_str());
		}
	}

	// Load the DLL
	hDll = LoadLibraryA(dllPath);
	if (!hDll) {
		char dbg[512];
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] LoadLibraryA failed (error %lu): %s\n",
			GetLastError(), dllPath);
		OutputDebugStringA(dbg);
		return false;
	}

	// Get exported functions
	fnGetInfo = (GetInfoFunc)GetProcAddress(hDll, "GetInfo");
	fnCreateMachine = (CreateMachineFunc)GetProcAddress(hDll, "CreateMachine");

	if (!fnGetInfo || !fnCreateMachine) {
		OutputDebugStringA("[BuzzBridgeHost32] DLL missing GetInfo or CreateMachine exports\n");
		FreeLibrary(hDll);
		hDll = nullptr;
		return false;
	}

	// Call GetInfo with SEH protection
	pInfo = nullptr;
	bool ok = SEH_Call([&]() {
		pInfo = fnGetInfo();
	});

	if (!ok || !ValidateInfo(pInfo)) {
		OutputDebugStringA("[BuzzBridgeHost32] GetInfo() failed or returned invalid info\n");
		pInfo = nullptr;
		FreeLibrary(hDll);
		hDll = nullptr;
		return false;
	}

	// Refuse to load machines known to corrupt the heap during CreateMachine.
	if (IsUnsupportedByLoadPath(pInfo)) {
		char dbg[192];
		snprintf(dbg, sizeof(dbg),
			"[BuzzBridgeHost32] Refusing to load blacklisted machine \"%s\" (would corrupt heap in CreateMachine)\n",
			pInfo->Name ? pInfo->Name : "(null)");
		OutputDebugStringA(dbg);
		pInfo = nullptr;
		FreeLibrary(hDll);
		hDll = nullptr;
		return false;
	}

	// Build parameter layout
	paramLayout.Build(pInfo);

	// Store callbacks reference to machine info
	callbacks.machineInfo = pInfo;

	loadedPath = dllPath;
	return true;
}

bool BuzzMachineLoader::InitMachine()
{
	OutputDebugStringA("[BuzzBridgeHost32] *** InitMachine ENTERED ***\n");
	if (!hDll || !fnCreateMachine || !pInfo) {
		OutputDebugStringA("[BuzzBridgeHost32] InitMachine: missing hDll/fnCreateMachine/pInfo\n");
		return false;
	}

	OutputDebugStringA("[BuzzBridgeHost32] InitMachine: calling CreateMachine...\n");

	// Create the machine instance
	pMachine = nullptr;
	bool ok = SEH_Call([&]() {
		pMachine = fnCreateMachine();
	});

	if (!ok || !pMachine) {
		OutputDebugStringA("[BuzzBridgeHost32] CreateMachine() failed\n");
		faulted = true;
		return false;
	}

	{
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] CreateMachine OK: pMachine=%p GlobalVals=%p TrackVals=%p\n",
			pMachine, pMachine->GlobalVals, pMachine->TrackVals);
		OutputDebugStringA(dbg);
	}

	// Apply per-machine post-CreateMachine workarounds. See the big comment
	// on ApplyPostCreateQuirks for why and which machines need this.
	ApplyPostCreateQuirks(pMachine, pInfo);

	// Note unsafe destructors up front so Unload doesn't try to call them.
	// Quirked machines also get hDll leaked because they typically use a
	// statically-linked CRT whose atexit handlers survive FreeLibrary.
	skipDestructor = HasBadDestructor(pInfo);
	skipDllUnload  = skipDestructor;

	// Set up the machine's host pointers
	pMachine->pMasterInfo = &masterInfo;
	pMachine->pCB = &callbacks;

	// Initialize attribute values with defaults.
	//
	// Normally we allocate our own int buffer and redirect pMachine->AttrVals
	// at it. This works for machines that read attributes through the
	// AttrVals pointer. HOWEVER, some machines (e.g. Jeskola Delay) set
	// AttrVals in their ctor to point at internal storage *inside the
	// instance*, and then read that internal storage DIRECTLY by hardcoded
	// offset (e.g. `fild [esi+0xEC]`) instead of via the AttrVals pointer.
	// If we blindly redirect AttrVals, our defaults never reach the
	// machine's internal storage — Init reads 0, computes a 0-sample delay
	// buffer, and the machine is silent (or, with a large uninit garbage
	// value before our zero-init quirk, hangs on a huge buffer allocation).
	//
	// Fix: when the machine's ctor has set AttrVals to a pointer INSIDE the
	// machine instance (verifiable via QuirkedInstanceSize), write defaults
	// through that existing pointer so the internal storage gets populated.
	// Fall back to our own external buffer otherwise. Don't trust a
	// ctor-provided pointer unconditionally — for machines we don't know
	// the layout of, it could be garbage and crash on write.
	if (pInfo->numAttributes > 0 && pInfo->Attributes) {
		int* target = nullptr;
		SIZE_T instSize = QuirkedInstanceSize(pInfo);
		if (instSize > 0) {
			int* ctorAttrVals = pMachine->AttrVals;
			char* base = reinterpret_cast<char*>(pMachine);
			char* end  = base + instSize;
			SIZE_T bytesNeeded = pInfo->numAttributes * sizeof(int);
			if (ctorAttrVals &&
			    reinterpret_cast<char*>(ctorAttrVals) >= base &&
			    reinterpret_cast<char*>(ctorAttrVals) + bytesNeeded <= end) {
				target = ctorAttrVals;
			}
		}
		if (!target) {
			attrVals.resize(pInfo->numAttributes);
			target = attrVals.data();
			pMachine->AttrVals = target;
		}
		for (int i = 0; i < pInfo->numAttributes; i++) {
			target[i] = pInfo->Attributes[i]->DefValue;
		}
		char dbg[192];
		snprintf(dbg, sizeof(dbg),
			"[BuzzBridgeHost32] AttrVals: target=%p (internal=%d) first=%d\n",
			(void*)target, (int)(target != attrVals.data()),
			pInfo->numAttributes > 0 ? target[0] : 0);
		OutputDebugStringA(dbg);
	}

	OutputDebugStringA("[BuzzBridgeHost32] InitMachine: calling Init...\n");

	// Initialize the machine with SEH protection.
	// For MDK machines (like Fuzzpilz Pottwal), Init (vtable[1]) is overridden by
	// CMDKMachineInterface::Init (FUN_10003d30) which:
	//   1. Calls pCB->GetNearestWaveLevel(-1,-1) to get an MDK object
	//   2. Stores MDK at machine+0x18 and wires up the ex interface
	//   3. Calls MDK->vtable[8](-1) which triggers the machine's MDKInit
	// Our MDK stub's vtable[8] (StubMDKSetup) calls machine->vtable[23] to
	// invoke the machine's actual MDKInit, which sets up buffers and FFT.
	ok = SEH_Call([&]() {
		pMachine->Init(nullptr);
	});

	if (!ok) {
		OutputDebugStringA("[BuzzBridgeHost32] Init() crashed\n");
		faulted = true;
		pMachine = nullptr;
		return false;
	}

	OutputDebugStringA("[BuzzBridgeHost32] Init() OK\n");

	// Check if MDK was detected during Init (via GetNearestWaveLevel(-1,-1))
	// and if MDKInit was actually called (via our vtable[10] tracking).
	if (callbacks.isMDKMachine && callbacks.mdkStub) {
		if (WasMDKInitCalled(callbacks.mdkStub)) {
			OutputDebugStringA("[BuzzBridgeHost32] MDK confirmed: MDKInit was called during Init\n");
		} else {
			OutputDebugStringA("[BuzzBridgeHost32] MDK stub created but MDKInit was NOT called\n");
		}
	}

	// Notify machine about attributes (must be after Init)
	if (pInfo->numAttributes > 0) {
		OutputDebugStringA("[BuzzBridgeHost32] Calling AttributesChanged...\n");
		ok = SEH_Call([&]() {
			pMachine->AttributesChanged();
		});
		if (!ok) OutputDebugStringA("[BuzzBridgeHost32] AttributesChanged crashed\n");
	}

	// Set number of tracks to minimum (for machines with track parameters)
	if (pInfo->numTrackParameters > 0 && pInfo->minTracks > 0) {
		char dbg[128];
		snprintf(dbg, sizeof(dbg), "[BuzzBridgeHost32] Calling SetNumTracks(%d)...\n", pInfo->minTracks);
		OutputDebugStringA(dbg);
		ok = SEH_Call([&]() {
			pMachine->SetNumTracks(pInfo->minTracks);
		});
		if (!ok) {
			OutputDebugStringA("[BuzzBridgeHost32] SetNumTracks crashed - marking faulted\n");
			faulted = true;
		}
	}

	OutputDebugStringA("[BuzzBridgeHost32] InitMachine: returning true\n");
	return true;
}

void BuzzMachineLoader::StopMachine()
{
	if (pMachine && !faulted) {
		SEH_Call([&]() {
			pMachine->Stop();
		});
	}
}

void BuzzMachineLoader::Unload()
{
	if (pMachine) {
		StopMachine();
		if (skipDestructor) {
			// Known-bad destructor — leak to avoid uncatchable crash.
		} else {
			SEH_Call([&]() {
#if defined(__MINGW32__) && !defined(_MSC_VER)
				// On the clang/mingw cross build CMachineInterface's dtor is
				// intentionally non-virtual (single-slot vtable to match MSVC,
				// see MachineInterface.h), so `delete pMachine` would not run
				// the machine's real destructor and would free the object with
				// the host's operator delete instead of the machine's. Invoke
				// the machine's own MSVC scalar-deleting destructor at vtable[0]
				// with flags=1 (destruct + operator delete in the machine's CRT).
				void** vt = *reinterpret_cast<void***>(pMachine);
				typedef void* (__thiscall *ScalarDeletingDtor)(void* self, unsigned int flags);
				reinterpret_cast<ScalarDeletingDtor>(vt[0])(pMachine, 1);
#else
				delete pMachine;
#endif
			});
		}
		pMachine = nullptr;
	}
	skipDestructor = false;

	pInfo = nullptr;
	fnGetInfo = nullptr;
	fnCreateMachine = nullptr;

	if (hDll) {
		if (!skipDllUnload) {
			FreeLibrary(hDll);
		}
		// For skipDllUnload machines we deliberately leak the HMODULE:
		// their static CRT registered atexit handlers in our CRT, and
		// FreeLibrary would leave dangling pointers that crash at exit.
		hDll = nullptr;
	}

	loadedPath.clear();
	faulted = false;
	callbacks.machineInfo = nullptr;
}

void BuzzMachineLoader::UpdateMasterInfo(double bpm, double sampleRate, int ticksPerBeat)
{
	if (bpm < 16.0) bpm = 125.0;
	if (bpm > 500.0) bpm = 500.0;
	if (sampleRate < 11050.0) sampleRate = 44100.0;
	if (ticksPerBeat < 1) ticksPerBeat = 4;
	if (ticksPerBeat > 32) ticksPerBeat = 32;

	masterInfo.BeatsPerMin = (int)bpm;
	masterInfo.TicksPerBeat = ticksPerBeat;
	masterInfo.SamplesPerSec = (int)sampleRate;
	masterInfo.SamplesPerTick = (int)((60.0 * sampleRate) / (bpm * ticksPerBeat));

	if (masterInfo.SamplesPerTick < 1)
		masterInfo.SamplesPerTick = 1;

	masterInfo.TicksPerSec = (float)sampleRate / (float)masterInfo.SamplesPerTick;
}

} // namespace BuzzVst
