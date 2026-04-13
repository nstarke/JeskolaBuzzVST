#pragma once

#include <windows.h>
#include "MachineInterface.h"
#include "BuzzCallbacks.h"
#include "BuzzParamLayout.h"
#include "BuzzWaveTable.h"
#include <string>
#include <vector>

namespace BuzzVst {

class BuzzMachineLoader {
public:
	BuzzMachineLoader();
	~BuzzMachineLoader();

	// Load a Buzz machine DLL. Returns true on success.
	bool Load(const char* dllPath);

	// Unload the currently loaded machine.
	void Unload();

	// Initialize the machine. Call after Load() and after setting up masterInfo.
	bool InitMachine();

	// Stop the machine (silence).
	void StopMachine();

	bool IsLoaded() const { return pMachine != nullptr; }
	bool IsFaulted() const { return faulted; }

	const CMachineInfo* GetInfo() const { return pInfo; }
	CMachineInterface* GetMachine() const { return pMachine; }
	CMachineInterfaceEx* GetMachineEx() const { return callbacks.machineInterfaceEx; }
	CMasterInfo* GetMasterInfo() { return &masterInfo; }
	BuzzCallbacks* GetCallbacks() { return &callbacks; }
	BuzzParamLayout* GetParamLayout() { return &paramLayout; }
	BuzzWaveTable* GetWaveTable() { return &waveTable; }
	const std::string& GetLoadedPath() const { return loadedPath; }

	// Update master info from VST3 transport
	void UpdateMasterInfo(double bpm, double sampleRate, int ticksPerBeat = 4);

private:
	HMODULE hDll = nullptr;
	const CMachineInfo* pInfo = nullptr;
	CMachineInterface* pMachine = nullptr;
	CMasterInfo masterInfo;
	BuzzCallbacks callbacks;
	BuzzParamLayout paramLayout;
	BuzzWaveTable waveTable;
	std::string loadedPath;
	bool faulted = false;
	// Some machines have destructors that recurse infinitely / blow the
	// stack (e.g. Ruff SPECCY II). For those, InitMachine sets this flag
	// and Unload skips `delete pMachine` to avoid an unrecoverable crash
	// during cleanup. The instance leaks until process exit; for the VST
	// plugin host this matters only on machine reload, and the quirked
	// machines are already unusual enough that the leak is acceptable.
	bool skipDestructor = false;
	// Some machines use a statically-linked CRT that registers atexit
	// handlers in the host CRT at LoadLibraryA time. FreeLibrary unmaps
	// the DLL but those handler function pointers remain in our atexit
	// list and crash at process exit. For such machines we leak the HMODULE.
	bool skipDllUnload = false;
	std::vector<int> attrVals; // attribute values (initialized to defaults)

	typedef CMachineInfo const* (__cdecl *GetInfoFunc)();
	typedef CMachineInterface* (__cdecl *CreateMachineFunc)();

	GetInfoFunc fnGetInfo = nullptr;
	CreateMachineFunc fnCreateMachine = nullptr;

	bool ValidateInfo(const CMachineInfo* info) const;
};

} // namespace BuzzVst
