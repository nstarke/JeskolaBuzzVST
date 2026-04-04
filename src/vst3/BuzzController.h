#pragma once

#include <windows.h>
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "plugids.h"
#include "../buzz/MachineInterface.h"
#include "../buzz/BuzzParamLayout.h"
#include "GearScanner.h"

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

namespace BuzzVst {

using namespace Steinberg;
using namespace Steinberg::Vst;

class BuzzPluginView;

// Shared controller base for both Generator and Effect variants.
// Pre-allocates hidden parameters at init and reveals them when a machine loads.
// Implements IMidiMapping so DAWs can route MIDI CCs to Buzz parameters.
class BuzzController : public EditController, public IMidiMapping {
public:
	BuzzController();
	~BuzzController() override;

	// EditController overrides
	tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
	tresult PLUGIN_API terminate() SMTG_OVERRIDE;
	tresult PLUGIN_API setComponentState(IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API notify(IMessage* message) SMTG_OVERRIDE;
	tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) SMTG_OVERRIDE;
	IPlugView* PLUGIN_API createView(const char* name) SMTG_OVERRIDE;

	// IMidiMapping
	tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 channel,
	                                                CtrlNumber midiControllerNumber,
	                                                ParamID& id) SMTG_OVERRIDE;

	// COM identity
	DELEGATE_REFCOUNT(EditController)
	tresult PLUGIN_API queryInterface(const char* iid, void** obj) SMTG_OVERRIDE;

	// Called by the GUI
	void onDllPathSelected(const std::string& path);
	void onGearDirSelected(const std::string& path);
	void onSamplesSelected(const std::vector<std::string>& wavPaths);

protected:
	virtual bool acceptsMachineType(int type) const = 0;

	bool loadMachineParameters(const std::string& path);
	void resetParameters();
	void sendDllPathToProcessor(const std::string& path);
	void scanGearDirectory(const std::string& path);

	std::string currentDllPath;
	std::string currentMachineName;
	std::string gearDirPath;
	std::vector<GearEntry> scannedMachines;
	int activeGlobalParams = 0;
	int activeTrackParams = 0;
	int currentNumTracks = 1;
	int machineMinTracks = 0;
	int machineMaxTracks = 0;
	int currentSampleRate = 0;

private:
	HMODULE hInfoDll = nullptr;
	const CMachineInfo* pInfo = nullptr;
	BuzzPluginView* activeView = nullptr;

	typedef CMachineInfo const* (__cdecl *GetInfoFunc)();

	void initPreallocatedParams();
	void pushParamInfoToView();
	void wireParamCallbacks(BuzzPluginView* view);

	// Background gear scan
	std::thread bgScanThread;
	std::atomic<bool> bgScanRunning{false};
	std::mutex scanResultMutex;
	std::vector<GearEntry> pendingScanResults;
	std::string pendingScanDir;
	bool hasPendingScanResults = false;

	void startBackgroundScan(const std::string& dir);
	void checkPendingScanResults();

	bool pendingParamRestart = false;
};

class GeneratorController : public BuzzController {
public:
	static FUnknown* createInstance(void*) { return (IEditController*)new GeneratorController; }
protected:
	bool acceptsMachineType(int type) const override { return type == MT_GENERATOR; }
};

class EffectController : public BuzzController {
public:
	static FUnknown* createInstance(void*) { return (IEditController*)new EffectController; }
protected:
	bool acceptsMachineType(int type) const override { return type == MT_EFFECT; }
};

} // namespace BuzzVst
