#include <windows.h>
#include "BuzzMachineLoader.h"
#include "../common/SEHGuard.h"
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

	// Spot-check first parameter pointer
	if (totalParams > 0) {
		__try {
			volatile const char* name = info->Parameters[0]->Name;
			(void)name;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// Validate track counts
	if (info->minTracks < 0 || info->maxTracks < 0)
		return false;
	if (info->maxTracks < info->minTracks)
		return false;

	return true;
}

bool BuzzMachineLoader::Load(const char* dllPath)
{
	Unload();
	faulted = false;

	if (!dllPath || !dllPath[0])
		return false;

	// Load the DLL
	hDll = LoadLibraryA(dllPath);
	if (!hDll) {
		OutputDebugStringA("[BuzzVst] Failed to load DLL: ");
		OutputDebugStringA(dllPath);
		OutputDebugStringA("\n");
		return false;
	}

	// Get exported functions
	fnGetInfo = (GetInfoFunc)GetProcAddress(hDll, "GetInfo");
	fnCreateMachine = (CreateMachineFunc)GetProcAddress(hDll, "CreateMachine");

	if (!fnGetInfo || !fnCreateMachine) {
		OutputDebugStringA("[BuzzVst] DLL missing GetInfo or CreateMachine exports\n");
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
		OutputDebugStringA("[BuzzVst] GetInfo() failed or returned invalid info\n");
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
	if (!hDll || !fnCreateMachine || !pInfo)
		return false;

	// Create the machine instance
	pMachine = nullptr;
	bool ok = SEH_Call([&]() {
		pMachine = fnCreateMachine();
	});

	if (!ok || !pMachine) {
		OutputDebugStringA("[BuzzVst] CreateMachine() failed\n");
		faulted = true;
		return false;
	}

	// Set up the machine's host pointers
	pMachine->pMasterInfo = &masterInfo;
	pMachine->pCB = &callbacks;

	// Allocate and initialize attribute values with defaults
	if (pInfo->numAttributes > 0 && pInfo->Attributes) {
		attrVals.resize(pInfo->numAttributes);
		for (int i = 0; i < pInfo->numAttributes; i++) {
			attrVals[i] = pInfo->Attributes[i]->DefValue;
		}
		pMachine->AttrVals = attrVals.data();
	}

	// Initialize the machine with SEH protection
	ok = SEH_Call([&]() {
		pMachine->Init(nullptr);
	});

	if (!ok) {
		OutputDebugStringA("[BuzzVst] Init() crashed\n");
		faulted = true;
		// Don't delete pMachine - it may have corrupted its state
		pMachine = nullptr;
		return false;
	}

	// Notify machine about attributes (must be after Init)
	if (pInfo->numAttributes > 0) {
		SEH_Call([&]() {
			pMachine->AttributesChanged();
		});
	}

	// Set number of tracks to minimum (for machines with track parameters)
	if (pInfo->numTrackParameters > 0 && pInfo->minTracks > 0) {
		SEH_Call([&]() {
			pMachine->SetNumTracks(pInfo->minTracks);
		});
	}

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
		SEH_Call([&]() {
			delete pMachine;
		});
		pMachine = nullptr;
	}

	pInfo = nullptr;
	fnGetInfo = nullptr;
	fnCreateMachine = nullptr;

	if (hDll) {
		FreeLibrary(hDll);
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
