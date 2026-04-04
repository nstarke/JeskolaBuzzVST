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
	// Pre-allocate global parameter slots.
	// Use kIsHidden | kIsReadOnly so they don't appear in automation/MIDI lists.
	// Active params will be revealed with kCanAutomate when a machine loads.
	for (int i = 0; i < kMaxGlobalParams; i++) {
		Steinberg::Vst::String128 name16;
		Steinberg::UString(name16, 128).fromAscii("");

		Steinberg::Vst::String128 units;
		units[0] = 0;

		parameters.addParameter(
			name16, units, 0, 0.0,
			ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly,
			kBuzzGlobalParamBase + i
		);
	}

	// Pre-allocate track parameter slots for all tracks
	for (int t = 0; t < kMaxTracks; t++) {
		for (int i = 0; i < kMaxTrackParams; i++) {
			Steinberg::Vst::String128 name16;
			Steinberg::UString(name16, 128).fromAscii("");

			Steinberg::Vst::String128 units;
			units[0] = 0;

			ParamID paramId = kBuzzTrackParamBase + t * kTrackParamStride + i;
			parameters.addParameter(
				name16, units, 0, 0.0,
				ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly,
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
			tresult rr = componentHandler->restartComponent(kParamTitlesChanged | kParamValuesChanged);
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

			// Tell host params changed (new track params revealed/hidden)
			if (componentHandler) {
				componentHandler->restartComponent(kParamTitlesChanged | kParamValuesChanged);
			}
		};

		view->onCheckScanResults = [this]() {
			checkPendingScanResults();
		};

		wireParamCallbacks(view);

		activeView = view;

		// Push current param info to the view
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
		scannedMachines = scanner.GetEntries();
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
			results = scanner.GetEntries();
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

	if (paramsLoaded && componentHandler) {
		componentHandler->restartComponent(kParamTitlesChanged | kParamValuesChanged);
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

				if (activeView) {
					activeView->setMachineName(currentMachineName);
					activeView->setDllPath(currentDllPath);
					activeView->setTrackInfo(std::max(1, (int)minT), (int)minT, (int)maxT);
				}
			}

			// Push param info to view (covers both direct-load and message-based paths)
			pushParamInfoToView();

			if (componentHandler) {
				OutputDebugStringA("[BuzzBridge] Controller: calling restartComponent\n");
				tresult rr = componentHandler->restartComponent(kParamTitlesChanged | kParamValuesChanged);
				char dbg[128];
				snprintf(dbg, sizeof(dbg), "[BuzzBridge] Controller: restartComponent returned %d\n", (int)rr);
				OutputDebugStringA(dbg);
				if (rr != kResultOk) {
					pendingParamRestart = true;
				}
			} else {
				OutputDebugStringA("[BuzzBridge] Controller: componentHandler is NULL, cannot restart\n");
				pendingParamRestart = true;
			}
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
			activeView->setMachineName("Load failed - check debug output");
		}
		return kResultOk;
	}

	return EditController::notify(message);
}

void BuzzController::resetParameters()
{
	// Reset all pre-allocated params back to hidden
	for (int i = 0; i < kMaxGlobalParams; i++) {
		Parameter* param = parameters.getParameter(kBuzzGlobalParamBase + i);
		if (param) {
			ParameterInfo& info = param->getInfo();
			info.flags = ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly;
			info.stepCount = 0;
			info.defaultNormalizedValue = 0.0;
			Steinberg::UString(info.title, 128).fromAscii("");
			info.shortTitle[0] = 0;
			info.units[0] = 0;
		}
	}

	for (int t = 0; t < kMaxTracks; t++) {
		for (int i = 0; i < kMaxTrackParams; i++) {
			ParamID paramId = kBuzzTrackParamBase + t * kTrackParamStride + i;
			Parameter* param = parameters.getParameter(paramId);
			if (!param) continue;

			ParameterInfo& info = param->getInfo();
			info.flags = ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly;
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

	// Update the GUI if open
	if (activeView) {
		activeView->setMachineName(currentMachineName);
		activeView->setDllPath(currentDllPath);
		activeView->setTrackInfo(initTracks, pInfo->minTracks, pInfo->maxTracks);
	}

	return true;
}

//------------------------------------------------------------------------
// IMidiMapping - maps MIDI CC numbers to Buzz parameter IDs.
// This allows DAWs to route MIDI CC knobs/faders to the machine's parameters.
// The mapping is sequential: CC0 -> first global param, CC1 -> second, etc.
//------------------------------------------------------------------------
tresult PLUGIN_API BuzzController::getMidiControllerAssignment(
	int32 busIndex, int16 /*channel*/,
	CtrlNumber midiControllerNumber, ParamID& id)
{
	if (busIndex != 0)
		return kResultFalse;

	// Map standard CCs (0-127) to global parameters sequentially
	if (midiControllerNumber < 128 && midiControllerNumber < activeGlobalParams) {
		id = kBuzzGlobalParamBase + midiControllerNumber;
		return kResultTrue;
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
		pvi.normalizedValue = param->getNormalized();
		infos.push_back(pvi);
	}

	// Track 0 params
	for (int i = 0; i < activeTrackParams; i++) {
		ParamID paramId = kBuzzTrackParamBase + i;
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
		pvi.normalizedValue = param->getNormalized();
		infos.push_back(pvi);
	}

	activeView->setParamInfo(infos);
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
