#pragma once

#include <windows.h>
#include <commctrl.h>
#include "public.sdk/source/common/pluginview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "GearScanner.h"
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>

namespace BuzzVst {

struct ParamViewInfo {
	Steinberg::Vst::ParamID paramId;
	std::string name;
	int32_t stepCount;
	int32_t minValue = 0;  // Buzz min value (for computing raw value from normalized)
	double normalizedValue;
	// Value descriptions for enum-like params (indexed by raw Buzz value - minValue)
	std::vector<std::string> valueDescriptions;
};

// Native Win32 GUI for selecting a Buzz machine DLL.
// Supports HiDPI via IPlugViewContentScaleSupport — the host provides a scale
// factor and we resize all controls and fonts accordingly.
class BuzzPluginView : public Steinberg::CPluginView,
                       public Steinberg::IPlugViewContentScaleSupport {
public:
	BuzzPluginView(const std::string& currentPath, const std::string& machineName,
	               const std::string& gearDir, const std::vector<GearEntry>& gearEntries,
	               bool isGenerator);
	~BuzzPluginView() override;

	// Callbacks
	std::function<void(const std::string& path)> onDllSelected;
	std::function<void(const std::string& gearDir)> onGearDirSelected;
	std::function<void(const std::vector<std::string>& wavPaths)> onSamplesSelected;
	// Callback for replacing/inserting a sample at a specific slot (1-based)
	std::function<void(int slotIndex, const std::string& wavPath)> onSampleSlotChanged;
	std::function<void(int delta)> onTrackCountChanged; // delta: +1 or -1
	std::function<void()> onCheckScanResults; // controller polls scan results
	std::function<void()> onDeferredParamUpdate; // called on UI thread after machine load
	std::function<bool()> onPollMachineLoad;    // returns true when load complete
	std::function<void(Steinberg::Vst::ParamID id)> onParamBeginEdit;
	std::function<void(Steinberg::Vst::ParamID id, double value)> onParamChanged;
	std::function<void(Steinberg::Vst::ParamID id)> onParamEndEdit;
	std::function<void(int presetIndex)> onPresetSelected;
	std::function<void(const std::string& presetName)> onSavePreset;

	// Update display
	void setMachineName(const std::string& name);
	void setDllPath(const std::string& path);
	void setGearDir(const std::string& dir);
	void setGearEntries(const std::vector<GearEntry>& entries);
	void setWaveSlots(const std::vector<std::string>& slotNames);
	void setTrackInfo(int current, int min, int max);
	void showScanningIndicator();
	void setParamInfo(const std::vector<ParamViewInfo>& params);
	void updateParamValue(Steinberg::Vst::ParamID id, double normalizedValue);
	void setPresetNames(const std::vector<std::string>& names);

	// IPlugView
	Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API canResize() SMTG_OVERRIDE { return Steinberg::kResultFalse; }

	// IPlugViewContentScaleSupport
	Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) SMTG_OVERRIDE;

	// FUnknown / CPluginView delegation
	OBJ_METHODS(BuzzPluginView, CPluginView)
	DEFINE_INTERFACES
		DEF_INTERFACE(IPlugView)
		DEF_INTERFACE(IPlugViewContentScaleSupport)
	END_DEFINE_INTERFACES(CPluginView)
	REFCOUNT_METHODS(CPluginView)

private:
	void createControls(HWND parent);
	void recreateControls();
	void populateMachineList();

	struct ParamControl {
		HWND hwndLabel = nullptr;
		HWND hwndTrackbar = nullptr;
		HWND hwndValueLabel = nullptr;
		Steinberg::Vst::ParamID paramId = 0;
		int32_t stepCount = 0;
		int32_t minValue = 0;
		std::vector<std::string> valueDescriptions;
	};

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK ParamPanelWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void onBrowseDllClicked();
	void onBrowseGearClicked();
	void onLoadSamplesClicked();
	void onMachineListDoubleClick();
	void onWaveListDoubleClick();
	void onScanComplete();
	void populateWaveList();
	void createParamControls();
	void destroyParamControls();

	// Scale a base pixel value by the current DPI factor
	int S(int basePixels) const { return (int)(basePixels * scaleFactor + 0.5f); }

	HWND hwndParent = nullptr;
	HWND hwndContainer = nullptr;
	HWND hwndTitle = nullptr;
	HWND hwndLabel = nullptr;
	HWND hwndPathLabel = nullptr;
	HWND hwndDllButton = nullptr;
	HWND hwndGearButton = nullptr;
	HWND hwndSamplesButton = nullptr;
	HWND hwndSamplesLabel = nullptr;
	HWND hwndWaveList = nullptr;
	HWND hwndAddTrackButton = nullptr;
	HWND hwndRemoveTrackButton = nullptr;
	HWND hwndTrackLabel = nullptr;
	HWND hwndGearLabel = nullptr;
	HWND hwndFilterEdit = nullptr;
	HWND hwndMachineList = nullptr;
	std::string machineFilter; // current filter text for machine list
	HWND hwndParamPanel = nullptr;
	HWND hwndParamLabel = nullptr;
	HWND hwndPresetCombo = nullptr;
	HWND hwndPresetLabel = nullptr;
	HWND hwndSavePresetButton = nullptr;

	// Fonts (recreated on scale change)
	HFONT hBoldFont = nullptr;
	HFONT hFont = nullptr;
	HFONT hSmallFont = nullptr;

	std::string dllPath;
	std::string machineName;
	std::string gearDir;
	std::vector<GearEntry> gearEntries;
	bool isGenerator;
	float scaleFactor = 1.0f;
	int viewHeight = 0;     // clamped height reported to host (may be < container)
	int contentHeight = 0;  // full layout height of the container
	int scrollY = 0;        // current scroll offset
	void updateScroll();    // recalculate scroll range and clamp position
	std::atomic<bool> scanning{false};
	std::thread scanThread;

	std::vector<ParamViewInfo> paramInfos;
	std::vector<ParamControl> paramControls;
	int paramScrollPos = 0;
	bool updatingFromHost = false; // guard to avoid feedback loops

	// Base (unscaled) dimensions
	std::vector<std::string> waveSlotNames; // slot names (index = slot-1)
	static const int kVisibleWaveSlots = 16; // how many slots to show in the list

	static const int kBaseWidth = 500;
	static const int kBaseHeight = 900;
	static const int kDllButtonID = 1001;
	static const int kGearButtonID = 1002;
	static const int kMachineListID = 1003;
	static const int kSamplesButtonID = 1004;
	static const int kWaveListID = 1005;
	static const int kAddTrackButtonID = 1006;
	static const int kRemoveTrackButtonID = 1007;
	static const int kFilterEditID = 1008;
	static const int kPresetComboID = 1009;
	static const int kSavePresetButtonID = 1010;
	static const int kParamSliderBaseID = 2000;
	static const int kParamLabelBaseID = 3000;

public:
	// Track state (public so controller can read for delta calculation)
	int currentTracks = 1;
	int minTracks = 0;
	int maxTracks = 0;
	static const UINT WM_SCAN_COMPLETE = WM_USER + 100;
	static const UINT WM_BG_SCAN_COMPLETE = WM_USER + 101;
	static const UINT WM_DEFERRED_PARAM_UPDATE = WM_USER + 102;

	HWND getContainerHWND() const { return hwndContainer; }
	void destroyFonts();
	void createFonts();
};

} // namespace BuzzVst
