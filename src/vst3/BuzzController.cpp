#include <windows.h>
#include "BuzzController.h"
#include "BuzzPluginView.h"
#include "ParameterMapping.h"
#include "../common/SEHGuard.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <cstdio>

namespace BuzzVst {

BuzzController::BuzzController()
{
}

BuzzController::~BuzzController()
{
	if (hInfoDll) {
		FreeLibrary(hInfoDll);
		hInfoDll = nullptr;
	}
}

void BuzzController::initPreallocatedParams()
{
	// Pre-allocate global parameter slots with kCanAutomate so they appear
	// in the host's automation/MIDI CC assignment lists immediately.
	// Some hosts cache the parameter list at init and ignore restartComponent,
	// so we must register them as automatable from the start.
	// Names are placeholders until a machine is loaded.
	for (int i = 0; i < kMaxGlobalParams; i++) {
		Steinberg::Vst::String128 name16;
		char slotName[32];
		snprintf(slotName, sizeof(slotName), "Param %d", i + 1);
		Steinberg::UString(name16, 128).fromAscii(slotName);

		Steinberg::Vst::String128 units;
		units[0] = 0;

		parameters.addParameter(
			name16, units, 0, 0.0,
			ParameterInfo::kCanAutomate,
			kBuzzGlobalParamBase + i
		);
	}

	// Pre-allocate track parameter slots for all tracks.
	// Track 0 params are always kCanAutomate (for MIDI CC mapping).
	// Other tracks start hidden and are revealed when tracks are added.
	for (int t = 0; t < kMaxTracks; t++) {
		for (int i = 0; i < kMaxTrackParams; i++) {
			Steinberg::Vst::String128 name16;
			char slotName[32];
			snprintf(slotName, sizeof(slotName), "T%d Param %d", t, i + 1);
			Steinberg::UString(name16, 128).fromAscii(slotName);

			Steinberg::Vst::String128 units;
			units[0] = 0;

			ParamID paramId = kBuzzTrackParamBase + t * kTrackParamStride + i;
			int32 flags = (t == 0)
				? ParameterInfo::kCanAutomate
				: (ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly);
			parameters.addParameter(
				name16, units, 0, 0.0,
				flags,
				paramId
			);
		}
	}
}

// Check if the default gear directory (%USERPROFILE%\Buzz\Gear) exists.
// Returns the path if it exists, empty string otherwise.
static std::string detectDefaultGearDir()
{
	char profileDir[MAX_PATH] = {};
	DWORD len = GetEnvironmentVariableA("USERPROFILE", profileDir, MAX_PATH);
	if (len == 0 || len >= MAX_PATH) return "";

	std::string gearPath = std::string(profileDir) + "\\Buzz\\Gear";

	DWORD attrs = GetFileAttributesA(gearPath.c_str());
	if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
		return gearPath;

	return "";
}

tresult PLUGIN_API BuzzController::initialize(FUnknown* context)
{
	tresult result = EditController::initialize(context);
	if (result != kResultOk)
		return result;

	// Bypass parameter (always visible)
	parameters.addParameter(
		STR16("Bypass"), nullptr, 1, 0,
		ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass,
		kBypassParamID
	);

	// Pre-allocate all parameter slots as hidden
	initPreallocatedParams();

	// Auto-detect default gear directory if none is set.
	// The installer places machines in %USERPROFILE%\Buzz\Gear.
	// Run on a background thread to avoid blocking the DAW during init.
	if (gearDirPath.empty()) {
		std::string defaultGear = detectDefaultGearDir();
		if (!defaultGear.empty()) {
			gearDirPath = defaultGear; // Set immediately so it persists
			startBackgroundScan(defaultGear);
		}
	}

	return kResultOk;
}

tresult PLUGIN_API BuzzController::terminate()
{
	// Wait for background scan to finish
	if (bgScanThread.joinable())
		bgScanThread.join();

	activeView = nullptr;
	if (hInfoDll) {
		FreeLibrary(hInfoDll);
		hInfoDll = nullptr;
	}
	pInfo = nullptr;
	return EditController::terminate();
}

IPlugView* PLUGIN_API BuzzController::createView(const char* name)
{
	if (strcmp(name, ViewType::kEditor) == 0) {
		// Check if a background scan completed while the view was closed
		checkPendingScanResults();

		// Retry deferred restartComponent if the host rejected it during init
		if (pendingParamRestart && componentHandler) {
			OutputDebugStringA("[BuzzBridge] Controller: retrying deferred restartComponent\n");
			tresult rr = componentHandler->restartComponent(kParamTitlesChanged | kParamValuesChanged | kMidiCCAssignmentChanged);
			char dbg[128];
			snprintf(dbg, sizeof(dbg), "[BuzzBridge] Controller: deferred restartComponent returned %d\n", (int)rr);
			OutputDebugStringA(dbg);
			if (rr == kResultOk) pendingParamRestart = false;
		}

		bool isGen = acceptsMachineType(MT_GENERATOR);
		auto* view = new BuzzPluginView(
			currentDllPath, currentMachineName,
			gearDirPath, scannedMachines, isGen);

		// If a scan is still running, show the indicator
		if (bgScanRunning) {
			view->showScanningIndicator();
		}

		view->onDllSelected = [this](const std::string& path) {
			onDllPathSelected(path);
		};

		view->onGearDirSelected = [this](const std::string& path) {
			onGearDirSelected(path);
		};

		view->onSamplesSelected = [this](const std::vector<std::string>& paths) {
			onSamplesSelected(paths);
		};

		view->onSampleSlotChanged = [this](int slotIndex, const std::string& wavPath) {
			// Send single slot load to processor
			std::vector<char> payload;
			int32_t num = 1;
			payload.insert(payload.end(), (char*)&num, (char*)&num + sizeof(num));
			int32_t slot = slotIndex;
			payload.insert(payload.end(), (char*)&slot, (char*)&slot + sizeof(slot));
			int32_t pathLen = (int32_t)wavPath.size();
			payload.insert(payload.end(), (char*)&pathLen, (char*)&pathLen + sizeof(pathLen));
			payload.insert(payload.end(), wavPath.begin(), wavPath.end());

			if (auto msg = owned(allocateMessage())) {
				msg->setMessageID("BuzzLoadWaveSlot");
				msg->getAttributes()->setBinary("Wave", payload.data(), (Steinberg::uint32)payload.size());
				sendMessage(msg);
			}
		};

		view->onTrackCountChanged = [this, view](int delta) {
			int newCount = view->currentTracks + delta;
			if (auto msg = owned(allocateMessage())) {
				msg->setMessageID("BuzzSetNumTracks");
				msg->getAttributes()->setInt("Count", newCount);
				sendMessage(msg);
			}
			// Update the view immediately for responsiveness
			view->setTrackInfo(newCount, view->minTracks, view->maxTracks);

			// Update active track param count and reveal/hide params for the new track count
			activeTrackParams = activeTrackParams; // unchanged per-track count
			for (int t = 0; t < kMaxTracks; t++) {
				for (int i = 0; i < activeTrackParams; i++) {
					ParamID paramId = kBuzzTrackParamBase + t * kTrackParamStride + i;
					Parameter* param = parameters.getParameter(paramId);
					if (!param) continue;
					ParameterInfo& info = param->getInfo();
					info.flags = (t < newCount)
						? ParameterInfo::kCanAutomate
						: (ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly);
				}
			}

			// Refresh the parameter sliders in the GUI
			pushParamInfoToView();

			// Tell host params changed (new track params revealed/hidden)
			if (componentHandler) {
				componentHandler->restartComponent(kParamTitlesChanged | kParamValuesChanged | kMidiCCAssignmentChanged);
			}
		};

		view->onCheckScanResults = [this]() {
			checkPendingScanResults();
		};

		wireParamCallbacks(view);

		activeView = view;

		// Push current machine state to the view
		if (machineMaxTracks > 0) {
			view->setTrackInfo(currentNumTracks, machineMinTracks, machineMaxTracks);
		}
		pushParamInfoToView();

		return view;
	}
	return nullptr;
}

void BuzzController::onGearDirSelected(const std::string& path)
{
	scanGearDirectory(path);

	// Update the view with the scan results
	if (activeView) {
		activeView->setGearDir(gearDirPath);
		activeView->setGearEntries(scannedMachines);
	}
}

void BuzzController::onSamplesSelected(const std::vector<std::string>& wavPaths)
{
	if (wavPaths.empty()) return;

	// Send wave file paths to the processor via message
	// Encode as: numPaths (int32) + [pathLen (int32) + pathData]...
	std::vector<char> payload;

	int32_t numPaths = (int32_t)wavPaths.size();
	payload.insert(payload.end(), (char*)&numPaths, (char*)&numPaths + sizeof(numPaths));

	for (auto& p : wavPaths) {
		int32_t pathLen = (int32_t)p.size();
		payload.insert(payload.end(), (char*)&pathLen, (char*)&pathLen + sizeof(pathLen));
		payload.insert(payload.end(), p.begin(), p.end());
	}

	if (auto msg = owned(allocateMessage())) {
		msg->setMessageID("BuzzLoadWaves");
		msg->getAttributes()->setBinary("Waves", payload.data(), (Steinberg::uint32)payload.size());
		sendMessage(msg);
	}
}

void BuzzController::scanGearDirectory(const std::string& path)
{
	gearDirPath = path;
	scannedMachines.clear();

	if (path.empty()) return;

	GearScanner scanner;
	if (scanner.Scan(path)) {
		// Filter control/no-output helpers (MIDI out, positional-audio listeners,
		// transport sync hacks). These declare MIF_NO_OUTPUT / MIF_CONTROL_MACHINE
		// and produce no audio by design — they shouldn't appear in the gear list.
		for (auto& e : scanner.GetEntries()) {
			if (e.flags & (MIF_NO_OUTPUT | MIF_CONTROL_MACHINE)) continue;
			scannedMachines.push_back(e);
		}
	}
}

void BuzzController::startBackgroundScan(const std::string& dir)
{
	if (bgScanRunning) return; // already scanning

	if (bgScanThread.joinable())
		bgScanThread.join();

	bgScanRunning = true;

	bgScanThread = std::thread([this, dir]() {
		OutputDebugStringA("[BuzzBridge] Background gear scan starting...\n");

		GearScanner scanner;
		std::vector<GearEntry> results;
		if (scanner.Scan(dir)) {
			for (auto& e : scanner.GetEntries()) {
				if (e.flags & (MIF_NO_OUTPUT | MIF_CONTROL_MACHINE)) continue;
				results.push_back(e);
			}
		}

		{
			std::lock_guard<std::mutex> lock(scanResultMutex);
			pendingScanResults = std::move(results);
			pendingScanDir = dir;
			hasPendingScanResults = true;
		}

		bgScanRunning = false;

		char dbgmsg[256];
		snprintf(dbgmsg, sizeof(dbgmsg), "[BuzzBridge] Background scan done: %d machines found\n",
			(int)pendingScanResults.size());
		OutputDebugStringA(dbgmsg);

		// Notify the view on the UI thread
		if (activeView) {
			HWND hwnd = activeView->getContainerHWND();
			if (hwnd && IsWindow(hwnd)) {
				PostMessage(hwnd, BuzzPluginView::WM_BG_SCAN_COMPLETE, 0, 0);
			}
		}
	});
}

void BuzzController::checkPendingScanResults()
{
	std::lock_guard<std::mutex> lock(scanResultMutex);
	if (!hasPendingScanResults) return;

	scannedMachines = std::move(pendingScanResults);
	gearDirPath = pendingScanDir;
	hasPendingScanResults = false;

	// Update the view if it's open
	if (activeView) {
		activeView->setGearDir(gearDirPath);
		activeView->setGearEntries(scannedMachines);
	}
}

// Extract display name from a DLL file path
static std::string NameFromPath(const std::string& path) {
	const char* fname = strrchr(path.c_str(), '\\');
	if (!fname) fname = strrchr(path.c_str(), '/');
	if (fname) fname++; else fname = path.c_str();
	std::string name = fname;
	auto dotPos = name.rfind('.');
	if (dotPos != std::string::npos)
		name = name.substr(0, dotPos);
	return name;
}

void BuzzController::onDllPathSelected(const std::string& path)
{
	if (path == currentDllPath)
		return;

	OutputDebugStringA("[BuzzBridge] onDllPathSelected: ");
	OutputDebugStringA(path.c_str());
	OutputDebugStringA("\n");

	bool paramsLoaded = loadMachineParameters(path);

	OutputDebugStringA(paramsLoaded
		? "[BuzzBridge] loadMachineParameters succeeded\n"
		: "[BuzzBridge] loadMachineParameters failed (expected in 64-bit mode)\n");

	// Always update the view and send to processor, even if param loading failed.
	// In 64-bit mode, the controller can't load 32-bit DLLs, but the processor
	// can via the bridge. The processor will send back machine info.
	currentDllPath = path;
	if (!paramsLoaded) {
		currentMachineName = NameFromPath(path) + " (loading...)";
	}

	// Always update the view
	if (activeView) {
		activeView->setMachineName(currentMachineName);
		activeView->setDllPath(currentDllPath);
	}

	// Always send to processor
	sendDllPathToProcessor(path);

	// Start a timer to poll for load completion (processor can't sendMessage back
	// from the audio thread without deadlocking). Timer fires on UI thread.
	if (activeView) {
		HWND hwnd = activeView->getContainerHWND();
		if (hwnd && IsWindow(hwnd)) {
			SetTimer(hwnd, 42 /*timer ID*/, 200 /*ms*/, nullptr);
		}
	}

	if (paramsLoaded && componentHandler) {
		componentHandler->restartComponent(kParamTitlesChanged | kParamValuesChanged | kMidiCCAssignmentChanged);
	}
}

void BuzzController::sendDllPathToProcessor(const std::string& path)
{
	OutputDebugStringA("[BuzzBridge] sendDllPathToProcessor: allocating message...\n");
	if (auto msg = owned(allocateMessage())) {
		msg->setMessageID("BuzzDllPath");
		msg->getAttributes()->setBinary("Path", path.c_str(), (Steinberg::uint32)path.size());
		tresult r = sendMessage(msg);
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "[BuzzBridge] sendDllPathToProcessor: sendMessage returned %d (0=ok)\n", (int)r);
		OutputDebugStringA(dbg);
	} else {
		OutputDebugStringA("[BuzzBridge] sendDllPathToProcessor: allocateMessage FAILED\n");
	}
}

tresult PLUGIN_API BuzzController::setComponentState(IBStream* state)
{
	if (!state) return kResultFalse;

	IBStreamer streamer(state, kLittleEndian);

	// Step 1: Read all data from the processor state stream

	// DLL path (length-prefixed string)
	char pathBuf[1024] = {0};
	Steinberg::int32 pathLen = 0;
	if (!streamer.readInt32(pathLen)) return kResultFalse;
	if (pathLen < 0 || pathLen >= 1024) return kResultFalse;
	if (pathLen > 0) {
		if (streamer.readRaw(pathBuf, pathLen) != pathLen) return kResultFalse;
		pathBuf[pathLen] = 0;
	}
	std::string savedDllPath = pathBuf;

	// Bypass
	Steinberg::int32 bypass = 0;
	if (!streamer.readInt32(bypass)) return kResultFalse;
	setParamNormalized(kBypassParamID, bypass ? 1.0 : 0.0);

	// Saved global parameter values
	std::vector<Steinberg::int32> savedGlobalValues;
	Steinberg::int32 numGlobal = 0;
	if (streamer.readInt32(numGlobal)) {
		if (numGlobal < 0) numGlobal = 0;
		if (numGlobal > kMaxGlobalParams) numGlobal = kMaxGlobalParams;
		if (numGlobal > 0) {
			savedGlobalValues.resize(numGlobal);
			for (Steinberg::int32 i = 0; i < numGlobal; i++) {
				if (!streamer.readInt32(savedGlobalValues[i])) break;
			}
		}
	}

	// Saved track parameter values (2D: numTracks × numParamsPerTrack)
	Steinberg::int32 savedNumTracks = 0;
	Steinberg::int32 savedNumTrackParams = 0;
	std::vector<std::vector<Steinberg::int32>> savedTrackValues;
	if (streamer.readInt32(savedNumTracks) && streamer.readInt32(savedNumTrackParams)) {
		if (savedNumTracks < 0) savedNumTracks = 0;
		if (savedNumTracks > kMaxTracks) return kResultFalse;
		if (savedNumTrackParams < 0) savedNumTrackParams = 0;
		if (savedNumTrackParams > kMaxTrackParams) return kResultFalse;

		if (savedNumTracks > 0 && savedNumTrackParams > 0) {
			savedTrackValues.resize(savedNumTracks);
			for (Steinberg::int32 t = 0; t < savedNumTracks; t++) {
				savedTrackValues[t].resize(savedNumTrackParams);
				for (Steinberg::int32 i = 0; i < savedNumTrackParams; i++) {
					if (!streamer.readInt32(savedTrackValues[t][i])) break;
				}
			}
		}
	}

	// Step 2: Load machine parameters (sets defaults on the pre-allocated params)
	if (!savedDllPath.empty() && savedDllPath != currentDllPath) {
		loadMachineParameters(savedDllPath);
	}

	// Step 3: Apply saved parameter values (overwriting defaults)
	if (pInfo) {
		// 32-bit mode: pInfo is available, apply immediately
		BuzzParamLayout layout;
		layout.Build(pInfo);
		auto& gSlots = layout.GetGlobalSlots();
		auto& tSlots = layout.GetTrackSlots();

		for (Steinberg::int32 i = 0; i < (Steinberg::int32)savedGlobalValues.size() &&
		     i < (Steinberg::int32)gSlots.size(); i++) {
			double normalized = ParameterMapping::BuzzToNormalized(savedGlobalValues[i], gSlots[i].param);
			setParamNormalized(kBuzzGlobalParamBase + i, normalized);
		}

		for (Steinberg::int32 t = 0; t < (Steinberg::int32)savedTrackValues.size(); t++) {
			for (Steinberg::int32 i = 0; i < (Steinberg::int32)savedTrackValues[t].size() &&
			     i < (Steinberg::int32)tSlots.size(); i++) {
				Steinberg::int32 paramId = kBuzzTrackParamBase + t * kTrackParamStride + i;
				double normalized = ParameterMapping::BuzzToNormalized(savedTrackValues[t][i], tSlots[i].param);
				setParamNormalized(paramId, normalized);
			}
		}
	} else if (!savedGlobalValues.empty() || !savedTrackValues.empty()) {
		// 64-bit mode: can't load the 32-bit DLL to get param info.
		// Defer value restoration until BuzzMachineLoaded message arrives
		// with the param info from the bridge host.
		OutputDebugStringA("[BuzzBridge] setComponentState: deferring param value restoration (no pInfo)\n");
		deferredGlobalValues = savedGlobalValues;
		deferredTrackValues = savedTrackValues;
		hasDeferredParamValues = true;

		// If BuzzMachineLoaded already arrived (params configured), apply now.
		// This handles the case where sendMessage is synchronous and the
		// message was delivered during processor->setState, before this runs.
		if (activeGlobalParams > 0 || activeTrackParams > 0) {
			OutputDebugStringA("[BuzzBridge] setComponentState: params already configured, applying immediately\n");
			applyDeferredParamValues();
		}
	}

	return kResultOk;
}

tresult PLUGIN_API BuzzController::setState(IBStream* state)
{
	if (!state) return kResultFalse;

	IBStreamer streamer(state, kLittleEndian);

	// Read DLL path
	char pathBuf[1024] = {0};
	Steinberg::int32 pathLen = 0;
	if (streamer.readInt32(pathLen) && pathLen > 0 && pathLen < 1024) {
		if (streamer.readRaw(pathBuf, pathLen) == pathLen) {
			pathBuf[pathLen] = 0;
			std::string newPath = pathBuf;
			if (newPath != currentDllPath && !newPath.empty()) {
				loadMachineParameters(newPath);
			}
		}
	}

	// Read gear directory path
	char gearBuf[1024] = {0};
	Steinberg::int32 gearLen = 0;
	if (streamer.readInt32(gearLen) && gearLen > 0 && gearLen < 1024) {
		if (streamer.readRaw(gearBuf, gearLen) == gearLen) {
			gearBuf[gearLen] = 0;
			std::string newGearDir = gearBuf;
			if (!newGearDir.empty()) {
				gearDirPath = newGearDir;
				startBackgroundScan(newGearDir);
			}
		}
	}

	return kResultOk;
}

tresult PLUGIN_API BuzzController::getState(IBStream* state)
{
	if (!state) return kResultFalse;

	IBStreamer streamer(state, kLittleEndian);

	// Write DLL path
	Steinberg::int32 pathLen = (Steinberg::int32)currentDllPath.size();
	streamer.writeInt32(pathLen);
	if (pathLen > 0) {
		streamer.writeRaw((void*)currentDllPath.c_str(), pathLen);
	}

	// Write gear directory path
	Steinberg::int32 gearLen = (Steinberg::int32)gearDirPath.size();
	streamer.writeInt32(gearLen);
	if (gearLen > 0) {
		streamer.writeRaw((void*)gearDirPath.c_str(), gearLen);
	}

	return kResultOk;
}

tresult PLUGIN_API BuzzController::notify(IMessage* message)
{
	if (!message) return kInvalidArgument;

	// Check if a background scan completed
	checkPendingScanResults();

	if (strcmp(message->getMessageID(), "BuzzMachineLoaded") == 0) {
		const void* data = nullptr;
		Steinberg::uint32 size = 0;
		if (message->getAttributes()->getBinary("Path", data, size) == kResultOk && size > 0) {
			std::string path((const char*)data, size);

			// Try to load params directly (works in 32-bit, fails in 64-bit)
			bool loaded = false;
			if (path != currentDllPath) {
				loaded = loadMachineParameters(path);
			}

			// If direct load failed (64-bit mode), configure params from the message
			if (!loaded) {
				resetParameters();
				currentDllPath = path;

				// Read machine name
				const void* nameData = nullptr;
				Steinberg::uint32 nameSize = 0;
				if (message->getAttributes()->getBinary("Name", nameData, nameSize) == kResultOk && nameSize > 0) {
					currentMachineName = std::string((const char*)nameData, nameSize);
				} else {
					currentMachineName = NameFromPath(path);
				}

				// Read track info
				Steinberg::int64 minT = 0, maxT = 0, numGlobal = 0, numTP = 0;
				message->getAttributes()->getInt("MinTracks", minT);
				message->getAttributes()->getInt("MaxTracks", maxT);
				message->getAttributes()->getInt("NumGlobal", numGlobal);
				message->getAttributes()->getInt("NumTrackParams", numTP);

				// (SampleRate attribute is still sent by the processor for
				// potential future use but no longer drives any UI — the
				// resampler now handles rate mismatches transparently.)

				// Decode parameter info blob and configure pre-allocated params
				const void* paramData = nullptr;
				Steinberg::uint32 paramSize = 0;
				if (message->getAttributes()->getBinary("Params", paramData, paramSize) == kResultOk && paramSize >= 4) {
					const char* ptr = (const char*)paramData;
					const char* end = ptr + paramSize;

					int32_t totalParams = *(const int32_t*)ptr; ptr += 4;

					int globalCount = std::min((int)numGlobal, (int)kMaxGlobalParams);
					int trackCount = std::min((int)numTP, (int)kMaxTrackParams);
					int initTracks = std::max(1, (int)minT);
					activeGlobalParams = globalCount;
					activeTrackParams = trackCount;
					paramMinValues.clear();
					paramMinValues.resize(globalCount + trackCount, 0);

					for (int32_t p = 0; p < totalParams && ptr + 28 <= end; p++) {
						int32_t type = *(const int32_t*)ptr; ptr += 4;
						int32_t mn   = *(const int32_t*)ptr; ptr += 4;
						int32_t mx   = *(const int32_t*)ptr; ptr += 4;
						int32_t nv   = *(const int32_t*)ptr; ptr += 4;
						int32_t dv   = *(const int32_t*)ptr; ptr += 4;
						int32_t fl   = *(const int32_t*)ptr; ptr += 4;
						int32_t nameLen = *(const int32_t*)ptr; ptr += 4;

						std::string paramName = "Param";
						if (nameLen > 0 && nameLen < 256 && ptr + nameLen <= end) {
							paramName = std::string(ptr, nameLen);
							ptr += nameLen;
						}

						int32_t stepCount = mx - mn;
						double defaultNorm = 0.0;
						if (stepCount > 0 && (fl & MPF_STATE)) {
							defaultNorm = (double)(dv - mn) / (double)stepCount;
						}

						// Cache min value for Buzz-to-normalized conversion
						if (p < (int)paramMinValues.size())
							paramMinValues[p] = mn;

						if (p < globalCount) {
							// Global param
							Parameter* param = parameters.getParameter(kBuzzGlobalParamBase + p);
							if (param) {
								ParameterInfo& info = param->getInfo();
								Steinberg::UString(info.title, 128).fromAscii(paramName.c_str());
								info.stepCount = stepCount;
								info.defaultNormalizedValue = defaultNorm;
								info.flags = ParameterInfo::kCanAutomate;
								param->setNormalized(defaultNorm);
							}
						} else {
							// Track param — configure for all tracks
							int tpi = p - globalCount;
							for (int t = 0; t < kMaxTracks; t++) {
								ParamID paramId = kBuzzTrackParamBase + t * kTrackParamStride + tpi;
								Parameter* param = parameters.getParameter(paramId);
								if (!param) continue;

								ParameterInfo& info = param->getInfo();
								char prefixed[256];
								snprintf(prefixed, sizeof(prefixed), "T%d: %s", t, paramName.c_str());
								Steinberg::UString(info.title, 128).fromAscii(prefixed);
								info.stepCount = stepCount;
								info.defaultNormalizedValue = defaultNorm;
								info.flags = (t < initTracks)
									? ParameterInfo::kCanAutomate
									: (ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly);
								param->setNormalized(defaultNorm);
							}
						}
					}

					OutputDebugStringA("[BuzzBridge] Controller: configured params from processor message\n");
				}

				// Decode value descriptions blob
				paramValueDescs.clear();
				const void* descData = nullptr;
				Steinberg::uint32 descSize = 0;
				if (message->getAttributes()->getBinary("ValueDescs", descData, descSize) == kResultOk && descSize >= 4) {
					const char* dp = (const char*)descData;
					const char* dend = dp + descSize;
					int32_t numEntries = *(const int32_t*)dp; dp += 4;

					for (int32_t e = 0; e < numEntries && dp + 8 <= dend; e++) {
						int32_t paramIdx = *(const int32_t*)dp; dp += 4;
						int32_t numVals = *(const int32_t*)dp; dp += 4;
						std::vector<std::string> descs(numVals);
						for (int32_t v = 0; v < numVals && dp + 4 <= dend; v++) {
							int32_t dLen = *(const int32_t*)dp; dp += 4;
							if (dLen > 0 && dLen < 256 && dp + dLen <= dend) {
								descs[v] = std::string(dp, dLen);
								dp += dLen;
							}
						}
						paramValueDescs[paramIdx] = descs;
					}

					char dbg[128];
					snprintf(dbg, sizeof(dbg), "[BuzzBridge] Controller: decoded %d value description entries\n", (int)paramValueDescs.size());
					OutputDebugStringA(dbg);
				}

				// Apply deferred param values from setComponentState (64-bit mode)
				if (hasDeferredParamValues) {
					applyDeferredParamValues();
				}

				// Store track counts for later use (createView, deferred update)
				currentNumTracks = std::max(1, (int)minT);
				machineMinTracks = (int)minT;
				machineMaxTracks = (int)maxT;

				if (activeView) {
					activeView->setMachineName(currentMachineName);
					activeView->setDllPath(currentDllPath);
					activeView->setTrackInfo(currentNumTracks, machineMinTracks, machineMaxTracks);
				}
			}

			// Defer ALL UI updates to the UI thread via PostMessage.
			// notify() can be called from the audio thread — any direct Win32
			// or host API calls from here can deadlock.
			if (activeView) {
				HWND hwnd = activeView->getContainerHWND();
				if (hwnd && IsWindow(hwnd)) {
					PostMessage(hwnd, BuzzPluginView::WM_DEFERRED_PARAM_UPDATE, 0, 0);
				}
			}
			pendingParamRestart = true;
		}
		return kResultOk;
	}

	// Handle wave table update from processor
	if (strcmp(message->getMessageID(), "BuzzWaveSlots") == 0) {
		const void* data = nullptr;
		Steinberg::uint32 size = 0;
		if (message->getAttributes()->getBinary("Slots", data, size) == kResultOk && size >= 4) {
			const char* ptr = (const char*)data;
			int32_t numSlots = *(const int32_t*)ptr;
			ptr += sizeof(int32_t);

			std::vector<std::string> slotNames(numSlots);
			for (int32_t i = 0; i < numSlots; i++) {
				if ((ptr - (const char*)data) + 4 > (int)size) break;
				int32_t nameLen = *(const int32_t*)ptr;
				ptr += sizeof(int32_t);
				if (nameLen > 0 && (ptr - (const char*)data) + nameLen <= (int)size) {
					slotNames[i] = std::string(ptr, nameLen);
					ptr += nameLen;
				}
			}

			if (activeView) {
				activeView->setWaveSlots(slotNames);
			}
		}
		return kResultOk;
	}

	// Handle machine load failure from processor
	if (strcmp(message->getMessageID(), "BuzzMachineLoadFailed") == 0) {
		OutputDebugStringA("[BuzzBridge] Controller: received load failure from processor\n");
		if (activeView) {
			// Try to get a reason string
			std::string reason = "Load failed";
			const void* reasonData = nullptr;
			Steinberg::uint32 reasonSize = 0;
			if (message->getAttributes()->getBinary("Reason", reasonData, reasonSize) == kResultOk && reasonSize > 0) {
				reason = std::string((const char*)reasonData, reasonSize);
			}
			activeView->setMachineName(reason);
		}
		return kResultOk;
	}

	return EditController::notify(message);
}

void BuzzController::resetParameters()
{
	// Reset global params back to placeholder names (keep kCanAutomate)
	for (int i = 0; i < kMaxGlobalParams; i++) {
		Parameter* param = parameters.getParameter(kBuzzGlobalParamBase + i);
		if (param) {
			ParameterInfo& info = param->getInfo();
			info.flags = ParameterInfo::kCanAutomate;
			info.stepCount = 0;
			info.defaultNormalizedValue = 0.0;
			char slotName[32];
			snprintf(slotName, sizeof(slotName), "Param %d", i + 1);
			Steinberg::UString(info.title, 128).fromAscii(slotName);
			info.shortTitle[0] = 0;
			info.units[0] = 0;
		}
	}

	// Reset track params: track 0 keeps kCanAutomate, others stay hidden
	for (int t = 0; t < kMaxTracks; t++) {
		for (int i = 0; i < kMaxTrackParams; i++) {
			ParamID paramId = kBuzzTrackParamBase + t * kTrackParamStride + i;
			Parameter* param = parameters.getParameter(paramId);
			if (!param) continue;

			ParameterInfo& info = param->getInfo();
			info.flags = (t == 0)
				? ParameterInfo::kCanAutomate
				: (ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly);
			info.stepCount = 0;
			info.defaultNormalizedValue = 0.0;

			char name[48];
			snprintf(name, sizeof(name), "T%d Param %d", t, i + 1);
			Steinberg::UString(info.title, 128).fromAscii(name);
			info.shortTitle[0] = 0;
			info.units[0] = 0;
		}
	}

	activeGlobalParams = 0;
	activeTrackParams = 0;
}

bool BuzzController::loadMachineParameters(const std::string& path)
{
	// Reset existing parameters to hidden
	resetParameters();

	if (hInfoDll) {
		FreeLibrary(hInfoDll);
		hInfoDll = nullptr;
	}
	pInfo = nullptr;
	currentDllPath.clear();
	currentMachineName.clear();

	if (path.empty()) {
		OutputDebugStringA("[BuzzBridge] loadMachineParameters: empty path\n");
		return false;
	}

	hInfoDll = LoadLibraryA(path.c_str());
	if (!hInfoDll) {
		char msg[512];
		snprintf(msg, sizeof(msg), "[BuzzBridge] LoadLibraryA failed (error %lu): %s\n",
			GetLastError(), path.c_str());
		OutputDebugStringA(msg);
		return false;
	}

	auto fnGetInfo = (GetInfoFunc)GetProcAddress(hInfoDll, "GetInfo");
	if (!fnGetInfo) {
		OutputDebugStringA("[BuzzBridge] DLL has no GetInfo export\n");
		FreeLibrary(hInfoDll);
		hInfoDll = nullptr;
		return false;
	}

	pInfo = nullptr;
	bool ok = SEH_Call([&]() {
		pInfo = fnGetInfo();
	});

	if (!ok) {
		OutputDebugStringA("[BuzzBridge] GetInfo() crashed\n");
		pInfo = nullptr;
		FreeLibrary(hInfoDll);
		hInfoDll = nullptr;
		return false;
	}

	if (!pInfo) {
		OutputDebugStringA("[BuzzBridge] GetInfo() returned null\n");
		FreeLibrary(hInfoDll);
		hInfoDll = nullptr;
		return false;
	}

	if (!acceptsMachineType(pInfo->Type)) {
		char msg[128];
		snprintf(msg, sizeof(msg), "[BuzzBridge] Machine type %d not accepted by this plugin\n", pInfo->Type);
		OutputDebugStringA(msg);
		pInfo = nullptr;
		FreeLibrary(hInfoDll);
		hInfoDll = nullptr;
		return false;
	}

	currentDllPath = path;
	currentMachineName = pInfo->Name ? pInfo->Name : "Unknown";

	// Build parameter layout
	BuzzParamLayout layout;
	layout.Build(pInfo);

	// Update global parameter slots with real machine info
	auto& gSlots = layout.GetGlobalSlots();
	activeGlobalParams = (int)gSlots.size();
	if (activeGlobalParams > kMaxGlobalParams)
		activeGlobalParams = kMaxGlobalParams;

	// Cache min values for Buzz-to-normalized conversion
	{
		auto& ts = layout.GetTrackSlots();
		paramMinValues.clear();
		paramMinValues.resize(activeGlobalParams + (int)ts.size(), 0);
		for (int i = 0; i < activeGlobalParams; i++)
			paramMinValues[i] = gSlots[i].param->MinValue;
		for (int i = 0; i < (int)ts.size(); i++)
			paramMinValues[activeGlobalParams + i] = ts[i].param->MinValue;
	}

	for (int i = 0; i < activeGlobalParams; i++) {
		const CMachineParameter* bp = gSlots[i].param;
		Parameter* param = parameters.getParameter(kBuzzGlobalParamBase + i);
		if (!param) continue;

		ParameterInfo& info = param->getInfo();

		Steinberg::UString(info.title, 128).fromAscii(bp->Name ? bp->Name : "Param");

		if (bp->Description) {
			Steinberg::UString(info.shortTitle, 128).fromAscii(bp->Description);
		}

		info.units[0] = 0;
		info.stepCount = ParameterMapping::GetStepCount(bp);
		info.defaultNormalizedValue = ParameterMapping::GetDefaultNormalized(bp);
		info.flags = ParameterInfo::kCanAutomate;

		param->setNormalized(info.defaultNormalizedValue);
	}

	// Update track parameter slots for all pre-allocated tracks
	auto& tSlots = layout.GetTrackSlots();
	activeTrackParams = (int)tSlots.size();
	if (activeTrackParams > kMaxTrackParams)
		activeTrackParams = kMaxTrackParams;

	int initTracks = std::max(1, pInfo->minTracks);

	for (int t = 0; t < kMaxTracks; t++) {
		for (int i = 0; i < activeTrackParams; i++) {
			ParamID paramId = kBuzzTrackParamBase + t * kTrackParamStride + i;
			Parameter* param = parameters.getParameter(paramId);
			if (!param) continue;

			const CMachineParameter* bp = tSlots[i].param;
			ParameterInfo& info = param->getInfo();

			char prefixedName[256];
			snprintf(prefixedName, sizeof(prefixedName), "T%d: %s", t, bp->Name ? bp->Name : "Param");
			Steinberg::UString(info.title, 128).fromAscii(prefixedName);

			info.units[0] = 0;
			info.stepCount = ParameterMapping::GetStepCount(bp);
			info.defaultNormalizedValue = ParameterMapping::GetDefaultNormalized(bp);

			// Show params for active tracks, hide for inactive
			if (t < initTracks) {
				info.flags = ParameterInfo::kCanAutomate;
			} else {
				info.flags = ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly;
			}

			param->setNormalized(info.defaultNormalizedValue);
		}
	}

	// Store track info
	currentNumTracks = initTracks;
	machineMinTracks = pInfo->minTracks;
	machineMaxTracks = pInfo->maxTracks;

	// Update the GUI if open
	if (activeView) {
		activeView->setMachineName(currentMachineName);
		activeView->setDllPath(currentDllPath);
		activeView->setTrackInfo(currentNumTracks, machineMinTracks, machineMaxTracks);
	}

	loadPresetsForMachine(path);

	return true;
}

//------------------------------------------------------------------------
// IMidiMapping - maps MIDI CC numbers to Buzz parameter IDs.
// This allows DAWs to route MIDI CC knobs/faders to the machine's parameters.
// The mapping is sequential: global params first, then track 0's params.
//------------------------------------------------------------------------
tresult PLUGIN_API BuzzController::getMidiControllerAssignment(
	int32 busIndex, int16 /*channel*/,
	CtrlNumber midiControllerNumber, ParamID& id)
{
	if (busIndex != 0)
		return kResultFalse;

	// Map CCs 0-127 to pre-allocated parameter slots.
	// Hosts may cache this mapping at init time (before a machine is loaded),
	// so we always map to all slots. CC 0-63 -> global params, CC 64-127 -> track 0 params.
	// Hidden/inactive params won't do anything until a machine reveals them.
	static const int kGlobalCCCount = 64;

	if (midiControllerNumber < 128) {
		int cc = (int)midiControllerNumber;
		if (cc < kGlobalCCCount) {
			id = kBuzzGlobalParamBase + cc;
			return kResultTrue;
		}
		int trackCC = cc - kGlobalCCCount;
		if (trackCC < kMaxTrackParams) {
			id = kBuzzTrackParamBase + trackCC; // track 0
			return kResultTrue;
		}
	}

	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API BuzzController::queryInterface(const char* iid, void** obj)
{
	QUERY_INTERFACE(iid, obj, IMidiMapping::iid, IMidiMapping)
	return EditController::queryInterface(iid, obj);
}

void BuzzController::pushParamInfoToView()
{
	if (!activeView) return;

	{
		char dbg[128];
		snprintf(dbg, sizeof(dbg), "[BuzzBridge] pushParamInfoToView: globalParams=%d trackParams=%d\n",
			activeGlobalParams, activeTrackParams);
		OutputDebugStringA(dbg);
	}

	std::vector<ParamViewInfo> infos;

	// Global params
	for (int i = 0; i < activeGlobalParams; i++) {
		Parameter* param = parameters.getParameter(kBuzzGlobalParamBase + i);
		if (!param) continue;

		ParameterInfo& pinfo = param->getInfo();
		if (pinfo.flags & ParameterInfo::kIsHidden) continue;

		ParamViewInfo pvi;
		pvi.paramId = kBuzzGlobalParamBase + i;

		// Convert title to std::string
		char nameBuf[256] = {};
		Steinberg::UString(pinfo.title, 128).toAscii(nameBuf, sizeof(nameBuf));
		pvi.name = nameBuf;
		pvi.stepCount = pinfo.stepCount;
		pvi.minValue = (i < (int)paramMinValues.size()) ? paramMinValues[i] : 0;
		pvi.normalizedValue = param->getNormalized();
		auto descIt = paramValueDescs.find(i);
		if (descIt != paramValueDescs.end()) pvi.valueDescriptions = descIt->second;
		infos.push_back(pvi);
	}

	// Track params for all active tracks
	for (int t = 0; t < kMaxTracks; t++) {
		for (int i = 0; i < activeTrackParams; i++) {
			ParamID paramId = kBuzzTrackParamBase + t * kTrackParamStride + i;
			Parameter* param = parameters.getParameter(paramId);
			if (!param) continue;

			ParameterInfo& pinfo = param->getInfo();
			if (pinfo.flags & ParameterInfo::kIsHidden) continue;

			ParamViewInfo pvi;
			pvi.paramId = paramId;

			char nameBuf[256] = {};
			Steinberg::UString(pinfo.title, 128).toAscii(nameBuf, sizeof(nameBuf));
			pvi.name = nameBuf;
			pvi.stepCount = pinfo.stepCount;
			int flatIdx = activeGlobalParams + i;
			pvi.minValue = (flatIdx < (int)paramMinValues.size()) ? paramMinValues[flatIdx] : 0;
			pvi.normalizedValue = param->getNormalized();
			auto descIt = paramValueDescs.find(flatIdx);
			if (descIt != paramValueDescs.end()) pvi.valueDescriptions = descIt->second;
			infos.push_back(pvi);
		}
	}

	activeView->setParamInfo(infos);
}

void BuzzController::loadPresetsForMachine(const std::string& dllPath)
{
	presetLoader.Clear();
	std::string prsPath = BuzzPresetLoader::FindPrsForDll(dllPath);
	if (prsPath.empty()) return;

	if (presetLoader.Load(prsPath)) {
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "[BuzzBridge] Loaded %d presets from %s\n",
			(int)presetLoader.GetPresets().size(), prsPath.c_str());
		OutputDebugStringA(dbg);

		if (activeView) {
			std::vector<std::string> names;
			for (auto& p : presetLoader.GetPresets())
				names.push_back(p.name);
			activeView->setPresetNames(names);
		}
	}
}

void BuzzController::applyPreset(int presetIndex)
{
	auto& presets = presetLoader.GetPresets();
	if (presetIndex < 0 || presetIndex >= (int)presets.size()) return;

	auto& preset = presets[presetIndex];
	char dbg[256];
	snprintf(dbg, sizeof(dbg), "[BuzzBridge] Applying preset '%s' (%d values)\n",
		preset.name.c_str(), (int)preset.paramValues.size());
	OutputDebugStringA(dbg);

	// Preset values are stored for STATE params only, in order.
	// Map them to the controller's param slots.
	int presetIdx = 0;

	// Global state params
	for (int i = 0; i < activeGlobalParams && presetIdx < (int)preset.paramValues.size(); i++) {
		Parameter* param = parameters.getParameter(kBuzzGlobalParamBase + i);
		if (!param) continue;
		int mn = (i < (int)paramMinValues.size()) ? paramMinValues[i] : 0;
		int stepCount = param->getInfo().stepCount;
		if (stepCount <= 0) continue;

		int buzzVal = preset.paramValues[presetIdx++];
		// Clamp to valid range (preset file is untrusted)
		if (buzzVal < mn) buzzVal = mn;
		if (buzzVal > mn + stepCount) buzzVal = mn + stepCount;
		double normalized = (double)(buzzVal - mn) / (double)stepCount;
		if (normalized < 0.0) normalized = 0.0;
		if (normalized > 1.0) normalized = 1.0;

		setParamNormalized(kBuzzGlobalParamBase + i, normalized);
		if (componentHandler) {
			componentHandler->beginEdit(kBuzzGlobalParamBase + i);
			componentHandler->performEdit(kBuzzGlobalParamBase + i, normalized);
			componentHandler->endEdit(kBuzzGlobalParamBase + i);
		}
	}

	// Track state params (track 0)
	for (int i = 0; i < activeTrackParams && presetIdx < (int)preset.paramValues.size(); i++) {
		ParamID paramId = kBuzzTrackParamBase + i;
		Parameter* param = parameters.getParameter(paramId);
		if (!param) continue;
		int flatIdx = activeGlobalParams + i;
		int mn = (flatIdx < (int)paramMinValues.size()) ? paramMinValues[flatIdx] : 0;
		int stepCount = param->getInfo().stepCount;
		if (stepCount <= 0) continue;

		int buzzVal = preset.paramValues[presetIdx++];
		// Clamp to valid range (preset file is untrusted)
		if (buzzVal < mn) buzzVal = mn;
		if (buzzVal > mn + stepCount) buzzVal = mn + stepCount;
		double normalized = (double)(buzzVal - mn) / (double)stepCount;
		if (normalized < 0.0) normalized = 0.0;
		if (normalized > 1.0) normalized = 1.0;

		setParamNormalized(paramId, normalized);
		if (componentHandler) {
			componentHandler->beginEdit(paramId);
			componentHandler->performEdit(paramId, normalized);
			componentHandler->endEdit(paramId);
		}
	}
}

void BuzzController::wireParamCallbacks(BuzzPluginView* view)
{
	view->onParamBeginEdit = [this](ParamID id) {
		beginEdit(id);
	};
	view->onParamChanged = [this](ParamID id, double value) {
		performEdit(id, value);
		setParamNormalized(id, value);
	};
	view->onParamEndEdit = [this](ParamID id) {
		endEdit(id);
	};
	view->onPresetSelected = [this](int presetIndex) {
		applyPreset(presetIndex);
	};
	view->onSavePreset = [this](const std::string& presetName) {
		if (currentDllPath.empty() || presetName.empty()) return;

		// Build preset from current parameter values
		BuzzPreset preset;
		preset.name = presetName;

		// Collect state param values (globals then tracks)
		for (int i = 0; i < activeGlobalParams; i++) {
			Parameter* param = parameters.getParameter(kBuzzGlobalParamBase + i);
			if (!param) continue;
			int stepCount = param->getInfo().stepCount;
			if (stepCount <= 0) continue;
			int mn = (i < (int)paramMinValues.size()) ? paramMinValues[i] : 0;
			int buzzVal = mn + (int)(param->getNormalized() * stepCount + 0.5);
			preset.paramValues.push_back(buzzVal);
		}
		for (int i = 0; i < activeTrackParams; i++) {
			Parameter* param = parameters.getParameter(kBuzzTrackParamBase + i);
			if (!param) continue;
			int stepCount = param->getInfo().stepCount;
			if (stepCount <= 0) continue;
			int flatIdx = activeGlobalParams + i;
			int mn = (flatIdx < (int)paramMinValues.size()) ? paramMinValues[flatIdx] : 0;
			int buzzVal = mn + (int)(param->getNormalized() * stepCount + 0.5);
			preset.paramValues.push_back(buzzVal);
		}

		// Set machine name if not already set
		if (presetLoader.GetMachineName().empty()) {
			presetLoader.SetMachineName(currentMachineName);
		}

		presetLoader.AddPreset(preset);

		// Save to .prs file
		std::string prsPath = BuzzPresetLoader::PrsPathForDll(currentDllPath);
		if (!prsPath.empty() && presetLoader.Save(prsPath)) {
			OutputDebugStringA("[BuzzBridge] Preset saved OK\n");

			// Backup to OneDrive if it exists, preserving Gear directory structure
			char profileDir[MAX_PATH] = {};
			DWORD len = GetEnvironmentVariableA("USERPROFILE", profileDir, MAX_PATH);
			if (len > 0 && len < MAX_PATH) {
				std::string oneDrive = std::string(profileDir) + "\\OneDrive";
				DWORD attr = GetFileAttributesA(oneDrive.c_str());
				if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
					// Extract relative path from Gear root (look for \Gear\ in the path)
					std::string lowerPrs = prsPath;
					for (auto& c : lowerPrs) c = (char)tolower((unsigned char)c);
					size_t gearPos = lowerPrs.find("\\gear\\");
					if (gearPos == std::string::npos) gearPos = lowerPrs.find("/gear/");
					if (gearPos != std::string::npos) {
						std::string relPath = prsPath.substr(gearPos); // e.g. \Gear\generators\Machine.prs
						std::string backupPath = oneDrive + "\\BuzzVST" + relPath;

						// Create directory tree
						std::string backupDir = backupPath;
						size_t lastSlash = backupDir.find_last_of("\\/");
						if (lastSlash != std::string::npos) {
							backupDir = backupDir.substr(0, lastSlash);
							CreateDirectoryA((oneDrive + "\\BuzzVST").c_str(), nullptr);
							CreateDirectoryA((oneDrive + "\\BuzzVST\\Gear").c_str(), nullptr);
							CreateDirectoryA(backupDir.c_str(), nullptr);
						}

						if (presetLoader.Save(backupPath)) {
							char dbg[512];
							snprintf(dbg, sizeof(dbg), "[BuzzBridge] Preset backup saved to: %s\n", backupPath.c_str());
							OutputDebugStringA(dbg);
						}
					}
				}
			}

			// Update combo box
			if (activeView) {
				std::vector<std::string> names;
				for (auto& p : presetLoader.GetPresets())
					names.push_back(p.name);
				activeView->setPresetNames(names);
			}
		}
	};
	view->onDeferredParamUpdate = [this]() {
		OutputDebugStringA("[BuzzBridge] Controller: deferred param update on UI thread\n");
		if (activeView) {
			activeView->setMachineName(currentMachineName);
			activeView->setDllPath(currentDllPath);
			activeView->setTrackInfo(currentNumTracks, machineMinTracks, machineMaxTracks);
		}
		pushParamInfoToView();
		loadPresetsForMachine(currentDllPath);
		if (componentHandler) {
			componentHandler->restartComponent(kParamTitlesChanged | kParamValuesChanged | kMidiCCAssignmentChanged);
		}
		pendingParamRestart = false;
	};
	view->onPollMachineLoad = [this]() -> bool {
		// Ask processor if it has a pending machine loaded
		if (auto msg = owned(allocateMessage())) {
			msg->setMessageID("BuzzPollMachineStatus");
			sendMessage(msg);
		}
		// The processor will reply with BuzzMachineLoaded if ready
		// Return true to stop timer if we got an update
		return !pendingParamRestart;
	};
}

void BuzzController::applyDeferredParamValues()
{
	if (!hasDeferredParamValues) return;
	hasDeferredParamValues = false;

	OutputDebugStringA("[BuzzBridge] Controller: applying deferred param values\n");

	// Apply deferred global values
	for (int i = 0; i < (int)deferredGlobalValues.size() && i < activeGlobalParams; i++) {
		Parameter* param = parameters.getParameter(kBuzzGlobalParamBase + i);
		if (!param) continue;
		int stepCount = param->getInfo().stepCount;
		if (stepCount <= 0) continue;
		int mn = (i < (int)paramMinValues.size()) ? paramMinValues[i] : 0;
		double normalized = (double)(deferredGlobalValues[i] - mn) / (double)stepCount;
		if (normalized < 0.0) normalized = 0.0;
		if (normalized > 1.0) normalized = 1.0;
		setParamNormalized(kBuzzGlobalParamBase + i, normalized);
	}

	// Apply deferred track values
	for (int t = 0; t < (int)deferredTrackValues.size(); t++) {
		for (int i = 0; i < (int)deferredTrackValues[t].size() && i < activeTrackParams; i++) {
			ParamID paramId = kBuzzTrackParamBase + t * kTrackParamStride + i;
			Parameter* param = parameters.getParameter(paramId);
			if (!param) continue;
			int stepCount = param->getInfo().stepCount;
			if (stepCount <= 0) continue;
			int mn = (activeGlobalParams + i < (int)paramMinValues.size())
				? paramMinValues[activeGlobalParams + i] : 0;
			double normalized = (double)(deferredTrackValues[t][i] - mn) / (double)stepCount;
			if (normalized < 0.0) normalized = 0.0;
			if (normalized > 1.0) normalized = 1.0;
			setParamNormalized(paramId, normalized);
		}
	}

	deferredGlobalValues.clear();
	deferredTrackValues.clear();

	char dbg[128];
	snprintf(dbg, sizeof(dbg), "[BuzzBridge] Controller: deferred params applied (g=%d t=%d)\n",
		activeGlobalParams, activeTrackParams);
	OutputDebugStringA(dbg);

	// Tell the host to re-read parameter values from the controller.
	// Without this, the host uses stale defaults and pushes them to the
	// processor, overwriting the correct values restored in setState.
	if (componentHandler) {
		tresult rr = componentHandler->restartComponent(kParamValuesChanged);
		snprintf(dbg, sizeof(dbg), "[BuzzBridge] Controller: restartComponent(kParamValuesChanged) = %d\n", (int)rr);
		OutputDebugStringA(dbg);
	}
}

tresult PLUGIN_API BuzzController::setParamNormalized(ParamID tag, ParamValue value)
{
	tresult result = EditController::setParamNormalized(tag, value);
	if (result == kResultOk && activeView) {
		activeView->updateParamValue(tag, value);
	}
	return result;
}

} // namespace BuzzVst
