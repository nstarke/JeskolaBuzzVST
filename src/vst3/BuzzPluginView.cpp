#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include "BuzzPluginView.h"
#include "../buzz/MachineInterface.h"
#include "pluginterfaces/gui/iplugview.h"

#include <cstdio>

namespace BuzzVst {

using namespace Steinberg;

static const wchar_t* kWindowClassName = L"BuzzPluginViewClass";
static const wchar_t* kParamPanelClassName = L"BuzzParamPanelClass";
static bool sClassRegistered = false;
static bool sParamPanelClassRegistered = false;

BuzzPluginView::BuzzPluginView(const std::string& currentPath,
                               const std::string& name,
                               const std::string& gDir,
                               const std::vector<GearEntry>& gEntries,
                               bool isGen)
	: CPluginView(nullptr)
	, dllPath(currentPath)
	, machineName(name)
	, gearDir(gDir)
	, gearEntries(gEntries)
	, isGenerator(isGen)
{
	ViewRect r(0, 0, kBaseWidth, kBaseHeight);
	setRect(r);
}

BuzzPluginView::~BuzzPluginView()
{
	if (onViewDestroyed)
		onViewDestroyed();
	scanning = false;
	if (scanThread.joinable())
		scanThread.join();
	destroyFonts();
	if (hwndContainer) {
		DestroyWindow(hwndContainer);
		hwndContainer = nullptr;
	}
}

void BuzzPluginView::destroyFonts()
{
	if (hBoldFont) { DeleteObject(hBoldFont); hBoldFont = nullptr; }
	if (hFont) { DeleteObject(hFont); hFont = nullptr; }
	if (hSmallFont) { DeleteObject(hSmallFont); hSmallFont = nullptr; }
}

void BuzzPluginView::createFonts()
{
	destroyFonts();
	hBoldFont = CreateFontW(S(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
	hFont = CreateFontW(S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
	hSmallFont = CreateFontW(S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

tresult PLUGIN_API BuzzPluginView::isPlatformTypeSupported(FIDString type)
{
	if (strcmp(type, kPlatformTypeHWND) == 0)
		return kResultTrue;
	return kResultFalse;
}

tresult PLUGIN_API BuzzPluginView::getSize(ViewRect* size)
{
	if (!size) return kInvalidArgument;

	contentHeight = S(kBaseHeight);
	int maxH = contentHeight;

	// Cap to screen work area so the window doesn't extend off-screen
	RECT workArea = {};
	if (SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0)) {
		int screenH = workArea.bottom - workArea.top;
		// Leave room for title bar / taskbar (~80px)
		int available = screenH - 80;
		if (available > 0 && maxH > available)
			maxH = available;
	}

	viewHeight = maxH;
	size->left = 0;
	size->top = 0;
	size->right = S(kBaseWidth);
	size->bottom = viewHeight;
	return kResultOk;
}

tresult PLUGIN_API BuzzPluginView::setContentScaleFactor(ScaleFactor factor)
{
	if (factor < 0.5f) factor = 0.5f;
	if (factor > 4.0f) factor = 4.0f;

	if (factor == scaleFactor)
		return kResultTrue;

	scaleFactor = factor;

	// Recompute capped height
	ViewRect sizeRect;
	getSize(&sizeRect);
	ViewRect r(0, 0, sizeRect.right, sizeRect.bottom);
	setRect(r);

	// If attached, recreate all controls at the new scale
	if (hwndContainer) {
		recreateControls();

		// Ask the host to resize our window
		if (plugFrame) {
			plugFrame->resizeView(this, &r);
		}
	}

	return kResultTrue;
}

tresult PLUGIN_API BuzzPluginView::attached(void* parent, FIDString type)
{
	if (strcmp(type, kPlatformTypeHWND) != 0)
		return kResultFalse;

	hwndParent = (HWND)parent;

	// Auto-detect DPI if the host hasn't called setContentScaleFactor.
	// Many hosts (VCV Rack, Renoise) don't call it, leaving scaleFactor=1.0
	// even on HiDPI displays.
	bool dpiChanged = false;
	if (scaleFactor == 1.0f) {
		HDC hdc = GetDC(hwndParent);
		if (hdc) {
			int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
			ReleaseDC(hwndParent, hdc);
			float detected = (float)dpiX / 96.0f;
			if (detected > 1.0f) {
				scaleFactor = detected;
				dpiChanged = true;
			}
		}
	}

	// If DPI was detected, update our rect and ask the host to resize
	if (dpiChanged) {
		ViewRect sizeRect;
		getSize(&sizeRect);
		ViewRect r(0, 0, sizeRect.right, sizeRect.bottom);
		setRect(r);
		if (plugFrame) {
			plugFrame->resizeView(this, &r);
		}
	}

	createControls(hwndParent);
	return CPluginView::attached(parent, type);
}

tresult PLUGIN_API BuzzPluginView::removed()
{
	scanning = false;
	if (scanThread.joinable())
		scanThread.join();

	destroyFonts();
	if (hwndContainer) {
		DestroyWindow(hwndContainer);
		hwndContainer = nullptr;
		hwndTitle = nullptr;
		hwndLabel = nullptr;
		hwndPathLabel = nullptr;
		hwndDllButton = nullptr;
		hwndGearButton = nullptr;
		hwndSamplesButton = nullptr;
		hwndSamplesLabel = nullptr;
		hwndWaveList = nullptr;
		hwndAddTrackButton = nullptr;
		hwndRemoveTrackButton = nullptr;
		hwndTrackLabel = nullptr;
		hwndGearLabel = nullptr;
		hwndFilterEdit = nullptr;
		hwndMachineList = nullptr;
		hwndParamPanel = nullptr;
		hwndParamLabel = nullptr;
		paramControls.clear();
	}
	hwndParent = nullptr;
	return CPluginView::removed();
}

void BuzzPluginView::recreateControls()
{
	// Save current state
	std::string savedMachineName = machineName;
	std::string savedDllPath = dllPath;
	std::string savedGearDir = gearDir;
	bool wasScanning = scanning.load();
	std::vector<ParamViewInfo> savedParamInfos = paramInfos;

	// Destroy old controls
	destroyFonts();
	paramControls.clear();
	if (hwndContainer) {
		DestroyWindow(hwndContainer);
		hwndContainer = nullptr;
	}

	// Create new controls at the new scale
	if (hwndParent)
		createControls(hwndParent);

	// Restore state
	if (!savedMachineName.empty()) setMachineName(savedMachineName);
	if (!savedDllPath.empty()) setDllPath(savedDllPath);
	if (!savedGearDir.empty()) setGearDir(savedGearDir);
	if (wasScanning) {
		showScanningIndicator();
	} else if (!gearEntries.empty()) {
		populateMachineList();
	}

	// Restore param info
	if (!savedParamInfos.empty()) {
		paramInfos = savedParamInfos;
		destroyParamControls();
		createParamControls();
	}
}

void BuzzPluginView::createControls(HWND parent)
{
	HINSTANCE hInst = GetModuleHandle(nullptr);

	// Init common controls for trackbar support
	INITCOMMONCONTROLSEX icc = {};
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_BAR_CLASSES;
	InitCommonControlsEx(&icc);

	if (!sClassRegistered) {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = WndProc;
		wc.hInstance = hInst;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
		wc.lpszClassName = kWindowClassName;
		RegisterClassExW(&wc);
		sClassRegistered = true;
	}

	if (!sParamPanelClassRegistered) {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = ParamPanelWndProc;
		wc.hInstance = hInst;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
		wc.lpszClassName = kParamPanelClassName;
		RegisterClassExW(&wc);
		sParamPanelClassRegistered = true;
	}

	createFonts();

	int w = S(kBaseWidth);
	int h = S(kBaseHeight);  // full content height (container)

	// Ensure viewHeight is computed
	if (viewHeight <= 0) {
		ViewRect sizeRect;
		getSize(&sizeRect);
	}
	contentHeight = h;

	hwndContainer = CreateWindowExW(
		0, kWindowClassName, L"",
		WS_CHILD | WS_VISIBLE,
		0, 0, w, h,
		parent, nullptr, hInst, this
	);

	scrollY = 0;
	updateScroll();

	int margin = S(10);
	int y = S(8);

	// Title
	const wchar_t* title = isGenerator ? L"Buzz Generator Bridge" : L"Buzz Effect Bridge";
	hwndTitle = CreateWindowExW(0, L"STATIC", title,
		WS_CHILD | WS_VISIBLE | SS_CENTER,
		margin, y, w - 2 * margin, S(22), hwndContainer, nullptr, hInst, nullptr);
	SendMessage(hwndTitle, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
	y += S(26);

	// Machine name label
	hwndLabel = CreateWindowExW(0, L"STATIC", L"No machine loaded",
		WS_CHILD | WS_VISIBLE | SS_CENTER,
		margin, y, w - 2 * margin, S(18), hwndContainer, nullptr, hInst, nullptr);
	SendMessage(hwndLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
	y += S(20);

	// DLL path label
	hwndPathLabel = CreateWindowExW(0, L"STATIC", L"",
		WS_CHILD | WS_VISIBLE | SS_CENTER | SS_PATHELLIPSIS,
		margin, y, w - 2 * margin, S(14), hwndContainer, nullptr, hInst, nullptr);
	SendMessage(hwndPathLabel, WM_SETFONT, (WPARAM)hSmallFont, TRUE);
	y += S(16);

	// (Sample-rate mismatch warning used to live here; removed because
	// BuzzResampler now interpolates host rate ↔ 44100 for us.)

	// Buttons row
	int btnW = S(180);
	int gap = S(20);
	int btnX1 = (w - 2 * btnW - gap) / 2;
	int btnX2 = btnX1 + btnW + gap;
	int btnH = S(30);

	hwndDllButton = CreateWindowExW(0, L"BUTTON", L"Load DLL...",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		btnX1, y, btnW, btnH, hwndContainer,
		(HMENU)(INT_PTR)kDllButtonID, hInst, nullptr);
	SendMessage(hwndDllButton, WM_SETFONT, (WPARAM)hFont, TRUE);

	hwndGearButton = CreateWindowExW(0, L"BUTTON", L"Set Gear Folder...",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		btnX2, y, btnW, btnH, hwndContainer,
		(HMENU)(INT_PTR)kGearButtonID, hInst, nullptr);
	SendMessage(hwndGearButton, WM_SETFONT, (WPARAM)hFont, TRUE);
	y += S(34);

	// Load Samples button + wave count label
	hwndSamplesButton = CreateWindowExW(0, L"BUTTON", L"Load Samples...",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		btnX1, y, btnW, btnH, hwndContainer,
		(HMENU)(INT_PTR)kSamplesButtonID, hInst, nullptr);
	SendMessage(hwndSamplesButton, WM_SETFONT, (WPARAM)hFont, TRUE);

	hwndSamplesLabel = CreateWindowExW(0, L"STATIC", L"Wave table: empty",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		btnX2, y + S(6), btnW, S(18), hwndContainer, nullptr, hInst, nullptr);
	SendMessage(hwndSamplesLabel, WM_SETFONT, (WPARAM)hSmallFont, TRUE);
	y += S(34);

	// Wave table listbox (shows slots 1..N)
	int innerMargin = S(12);
	int waveListHeight = S(14) * kVisibleWaveSlots + S(4);
	hwndWaveList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
		WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
		innerMargin, y, w - 2 * innerMargin, waveListHeight,
		hwndContainer, (HMENU)(INT_PTR)kWaveListID, hInst, nullptr);
	SendMessage(hwndWaveList, WM_SETFONT, (WPARAM)hSmallFont, TRUE);
	populateWaveList();
	y += waveListHeight + S(4);

	// Track controls row: [-] [Tracks: 1/8] [+]
	int trackBtnW = S(30);
	int trackLabelW = S(120);
	int trackRowX = innerMargin;

	hwndRemoveTrackButton = CreateWindowExW(0, L"BUTTON", L"-",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		trackRowX, y, trackBtnW, S(24), hwndContainer,
		(HMENU)(INT_PTR)kRemoveTrackButtonID, hInst, nullptr);
	SendMessage(hwndRemoveTrackButton, WM_SETFONT, (WPARAM)hFont, TRUE);

	hwndTrackLabel = CreateWindowExW(0, L"STATIC", L"Tracks: 1",
		WS_CHILD | WS_VISIBLE | SS_CENTER,
		trackRowX + trackBtnW + S(4), y + S(3), trackLabelW, S(18),
		hwndContainer, nullptr, hInst, nullptr);
	SendMessage(hwndTrackLabel, WM_SETFONT, (WPARAM)hSmallFont, TRUE);

	hwndAddTrackButton = CreateWindowExW(0, L"BUTTON", L"+",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		trackRowX + trackBtnW + S(8) + trackLabelW, y, trackBtnW, S(24), hwndContainer,
		(HMENU)(INT_PTR)kAddTrackButtonID, hInst, nullptr);
	SendMessage(hwndAddTrackButton, WM_SETFONT, (WPARAM)hFont, TRUE);

	setTrackInfo(currentTracks, minTracks, maxTracks);
	y += S(28);

	// Preset combo box
	hwndPresetLabel = CreateWindowExW(0, L"STATIC", L"Preset:",
		WS_CHILD | SS_LEFT,
		innerMargin, y + S(2), S(45), S(16), hwndContainer, nullptr, hInst, nullptr);
	SendMessage(hwndPresetLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

	int presetBtnW = S(75);
	hwndPresetCombo = CreateWindowExW(0, L"COMBOBOX", L"",
		WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
		innerMargin + S(48), y, w - 2 * innerMargin - S(48) - presetBtnW - S(4), S(200),
		hwndContainer, (HMENU)(INT_PTR)kPresetComboID, hInst, nullptr);
	SendMessage(hwndPresetCombo, WM_SETFONT, (WPARAM)hSmallFont, TRUE);

	hwndSavePresetButton = CreateWindowExW(0, L"BUTTON", L"Save Preset",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		w - innerMargin - presetBtnW, y, presetBtnW, S(22),
		hwndContainer, (HMENU)(INT_PTR)kSavePresetButtonID, hInst, nullptr);
	SendMessage(hwndSavePresetButton, WM_SETFONT, (WPARAM)hSmallFont, TRUE);
	y += S(28);

	// Parameters label
	hwndParamLabel = CreateWindowExW(0, L"STATIC", L"Parameters",
		WS_CHILD | WS_VISIBLE | SS_LEFT,
		innerMargin, y, w - 2 * innerMargin, S(16), hwndContainer, nullptr, hInst, nullptr);
	SendMessage(hwndParamLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
	y += S(18);

	// The parameter panel and machine list share the remaining vertical space.
	// Calculate how much room is left for both, then split it: ~40% params, ~60% machine list.
	int fixedBelowParams = S(18) + S(24) + S(8); // gear label + filter edit + bottom margin
	int remainingSpace = h - y - fixedBelowParams;
	int paramPanelHeight = std::max(S(120), remainingSpace * 2 / 5);  // 40% of remaining, min 120
	int machineListHeight = std::max(S(80), remainingSpace - paramPanelHeight); // rest for machine list

	// Scrollable parameter panel
	hwndParamPanel = CreateWindowExW(WS_EX_CLIENTEDGE, kParamPanelClassName, L"",
		WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
		innerMargin, y, w - 2 * innerMargin, paramPanelHeight,
		hwndContainer, nullptr, hInst, this);
	y += paramPanelHeight + S(4);

	createParamControls();

	// Gear directory label
	hwndGearLabel = CreateWindowExW(0, L"STATIC", L"Gear folder: (not set)",
		WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
		innerMargin, y, w - 2 * innerMargin, S(14), hwndContainer, nullptr, hInst, nullptr);
	SendMessage(hwndGearLabel, WM_SETFONT, (WPARAM)hSmallFont, TRUE);
	y += S(18);

	// Machine filter text input
	hwndFilterEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
		innerMargin, y, w - 2 * innerMargin, S(22),
		hwndContainer, (HMENU)(INT_PTR)kFilterEditID, hInst, nullptr);
	SendMessage(hwndFilterEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
	SendMessageA(hwndFilterEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter machines...");
	y += S(24);

	// Machine listbox
	hwndMachineList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
		WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
		innerMargin, y, w - 2 * innerMargin, machineListHeight,
		hwndContainer, (HMENU)(INT_PTR)kMachineListID, hInst, nullptr);
	SendMessage(hwndMachineList, WM_SETFONT, (WPARAM)hFont, TRUE);

	// Restore initial state
	if (!machineName.empty()) setMachineName(machineName);
	if (!dllPath.empty()) setDllPath(dllPath);
	if (!gearDir.empty()) setGearDir(gearDir);
	if (!gearEntries.empty()) populateMachineList();
}

void BuzzPluginView::updateScroll()
{
	if (!hwndContainer || !hwndParent) return;

	if (contentHeight > viewHeight && viewHeight > 0) {
		// Enable scrollbar on the parent (the host-provided HWND)
		SCROLLINFO si = {};
		si.cbSize = sizeof(si);
		si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		si.nMin = 0;
		si.nMax = contentHeight - 1;
		si.nPage = viewHeight;
		si.nPos = scrollY;
		SetScrollInfo(hwndParent, SB_VERT, &si, TRUE);
		ShowScrollBar(hwndParent, SB_VERT, TRUE);

		// Offset the container window upward by scrollY
		SetWindowPos(hwndContainer, nullptr, 0, -scrollY, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	} else {
		ShowScrollBar(hwndParent, SB_VERT, FALSE);
		scrollY = 0;
		SetWindowPos(hwndContainer, nullptr, 0, 0, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

LRESULT CALLBACK BuzzPluginView::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_CREATE) {
		auto* cs = (CREATESTRUCT*)lParam;
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
		return 0;
	}

	auto* self = (BuzzPluginView*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

	switch (msg) {
		case WM_COMMAND:
			if (self) {
				WORD id = LOWORD(wParam);
				WORD code = HIWORD(wParam);

				if (id == kDllButtonID && code == BN_CLICKED) {
					self->onBrowseDllClicked();
					return 0;
				}
				if (id == kGearButtonID && code == BN_CLICKED) {
					self->onBrowseGearClicked();
					return 0;
				}
				if (id == kSamplesButtonID && code == BN_CLICKED) {
					self->onLoadSamplesClicked();
					return 0;
				}
				if (id == kFilterEditID && code == EN_CHANGE) {
					char buf[256] = {};
					GetWindowTextA(self->hwndFilterEdit, buf, sizeof(buf));
					self->machineFilter = buf;
					self->populateMachineList();
					return 0;
				}
				if (id == kMachineListID && code == LBN_DBLCLK) {
					self->onMachineListDoubleClick();
					return 0;
				}
				if (id == kWaveListID && code == LBN_DBLCLK) {
					self->onWaveListDoubleClick();
					return 0;
				}
				if (id == kAddTrackButtonID && code == BN_CLICKED) {
					if (self->onTrackCountChanged && self->currentTracks < self->maxTracks)
						self->onTrackCountChanged(+1);
					return 0;
				}
				if (id == kRemoveTrackButtonID && code == BN_CLICKED) {
					if (self->onTrackCountChanged && self->currentTracks > self->minTracks)
						self->onTrackCountChanged(-1);
					return 0;
				}
				if (id == kPresetComboID && code == CBN_SELCHANGE) {
					int sel = (int)SendMessage(self->hwndPresetCombo, CB_GETCURSEL, 0, 0);
					if (sel > 0 && self->onPresetSelected)
						self->onPresetSelected(sel - 1);
					return 0;
				}
				if (id == kSavePresetButtonID && code == BN_CLICKED) {
					if (self->onSavePreset) {
						// Build a DLGTEMPLATE in memory for a simple input dialog
						#pragma pack(push, 4)
						struct {
							DLGTEMPLATE dlg;
							WORD menu, cls, title;
							// Controls follow (edit + OK + Cancel)
						} tmpl = {};
						#pragma pack(pop)

						// Use simpler approach: editable combo box text as preset name
						// Prompt with a simple dialog template created at runtime
						static char s_presetName[256] = {};
						strcpy(s_presetName, "My Preset");

						// Minimal approach: use the combo box's edit field
						// Actually just use a simple Windows prompt
						HWND hParent = self->hwndContainer;
						// Create popup with edit
						HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
							L"#32770", L"Save Preset",
							WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_VISIBLE,
							0, 0, 300, 130, hParent, nullptr, GetModuleHandle(nullptr), nullptr);
						if (!hDlg) return 0;

						// Center on parent
						RECT pr; GetWindowRect(hParent, &pr);
						SetWindowPos(hDlg, HWND_TOP,
							(pr.left + pr.right) / 2 - 150, (pr.top + pr.bottom) / 2 - 65,
							0, 0, SWP_NOSIZE);

						HINSTANCE hI = GetModuleHandle(nullptr);
						HWND hLabel = CreateWindowExW(0, L"STATIC", L"Preset name:",
							WS_CHILD | WS_VISIBLE, 10, 10, 270, 18, hDlg, nullptr, hI, nullptr);
						HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"My Preset",
							WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
							10, 30, 270, 24, hDlg, (HMENU)100, hI, nullptr);
						HWND hOK = CreateWindowExW(0, L"BUTTON", L"Save",
							WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
							70, 65, 70, 28, hDlg, (HMENU)IDOK, hI, nullptr);
						HWND hCan = CreateWindowExW(0, L"BUTTON", L"Cancel",
							WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
							150, 65, 70, 28, hDlg, (HMENU)IDCANCEL, hI, nullptr);
						SendMessage(hLabel, WM_SETFONT, (WPARAM)self->hFont, TRUE);
						SendMessage(hEdit, WM_SETFONT, (WPARAM)self->hFont, TRUE);
						SendMessage(hOK, WM_SETFONT, (WPARAM)self->hFont, TRUE);
						SendMessage(hCan, WM_SETFONT, (WPARAM)self->hFont, TRUE);
						SendMessage(hEdit, EM_SETSEL, 0, -1);
						SetFocus(hEdit);
						EnableWindow(hParent, FALSE);

						MSG m;
						bool done = false;
						while (!done && GetMessage(&m, nullptr, 0, 0)) {
							if (m.hwnd == hDlg || IsChild(hDlg, m.hwnd)) {
								if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
									GetWindowTextA(hEdit, s_presetName, sizeof(s_presetName));
									if (s_presetName[0]) self->onSavePreset(s_presetName);
									done = true; continue;
								}
								if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
									done = true; continue;
								}
								if (m.message == WM_COMMAND) {
									if (LOWORD(m.wParam) == IDOK) {
										GetWindowTextA(hEdit, s_presetName, sizeof(s_presetName));
										if (s_presetName[0]) self->onSavePreset(s_presetName);
										done = true; continue;
									}
									if (LOWORD(m.wParam) == IDCANCEL) {
										done = true; continue;
									}
								}
							}
							if (m.message == WM_CLOSE || m.message == WM_QUIT) {
								done = true; continue;
							}
							if (!IsDialogMessage(hDlg, &m)) {
								TranslateMessage(&m);
								DispatchMessage(&m);
							}
						}
						EnableWindow(hParent, TRUE);
						SetForegroundWindow(hParent);
						DestroyWindow(hDlg);
					}
					return 0;
				}
			}
			break;

		case WM_MOUSEWHEEL:
			if (self) {
				// Forward mouse wheel to the param panel if cursor is over it
				if (self->hwndParamPanel) {
					POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
					ScreenToClient(hWnd, &pt);
					RECT panelRect;
					GetWindowRect(self->hwndParamPanel, &panelRect);
					MapWindowPoints(HWND_DESKTOP, hWnd, (POINT*)&panelRect, 2);
					if (PtInRect(&panelRect, pt)) {
						SendMessage(self->hwndParamPanel, WM_MOUSEWHEEL, wParam, lParam);
						return 0;
					}
				}
				// Otherwise scroll the whole view if content is taller than viewport
				if (self->contentHeight > self->viewHeight) {
					int delta = GET_WHEEL_DELTA_WPARAM(wParam);
					int oldPos = self->scrollY;
					int maxScroll = self->contentHeight - self->viewHeight;
					self->scrollY -= delta / 2;
					if (self->scrollY < 0) self->scrollY = 0;
					if (self->scrollY > maxScroll) self->scrollY = maxScroll;
					if (self->scrollY != oldPos) self->updateScroll();
					return 0;
				}
			}
			break;

		case WM_ERASEBKGND: {
			HDC hdc = (HDC)wParam;
			RECT rc;
			GetClientRect(hWnd, &rc);
			FillRect(hdc, &rc, (HBRUSH)(COLOR_3DFACE + 1));
			return 1;
		}

		default:
			if (msg == WM_SCAN_COMPLETE && self) {
				self->onScanComplete();
				return 0;
			}
			if (msg == WM_BG_SCAN_COMPLETE && self) {
				if (self->onCheckScanResults) self->onCheckScanResults();
				return 0;
			}
			if (msg == WM_DEFERRED_PARAM_UPDATE && self) {
				if (self->onCheckScanResults) self->onCheckScanResults();
				if (self->onDeferredParamUpdate) self->onDeferredParamUpdate();
				return 0;
			}
			if (msg == WM_TIMER && wParam == 42 && self) {
				// Poll timer: check if deferred machine load completed
				if (self->onPollMachineLoad) {
					bool done = self->onPollMachineLoad();
					if (done) {
						KillTimer(hWnd, 42);
					}
				}
				return 0;
			}
			break;

		case WM_VSCROLL:
			if (self && self->contentHeight > self->viewHeight) {
				int oldPos = self->scrollY;
				int maxScroll = self->contentHeight - self->viewHeight;
				switch (LOWORD(wParam)) {
					case SB_LINEUP:        self->scrollY -= 20; break;
					case SB_LINEDOWN:      self->scrollY += 20; break;
					case SB_PAGEUP:        self->scrollY -= self->viewHeight; break;
					case SB_PAGEDOWN:      self->scrollY += self->viewHeight; break;
					case SB_THUMBTRACK:
					case SB_THUMBPOSITION: self->scrollY = HIWORD(wParam); break;
				}
				if (self->scrollY < 0) self->scrollY = 0;
				if (self->scrollY > maxScroll) self->scrollY = maxScroll;
				if (self->scrollY != oldPos) self->updateScroll();
				return 0;
			}
			break;

	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void BuzzPluginView::onBrowseDllClicked()
{
	char filename[MAX_PATH] = {};
	if (!dllPath.empty())
		strncpy(filename, dllPath.c_str(), MAX_PATH - 1);

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwndContainer;
	ofn.lpstrFilter = "Buzz Machine DLLs (*.dll)\0*.dll\0All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = isGenerator
		? "Select a Buzz Generator DLL"
		: "Select a Buzz Effect DLL";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn)) {
		dllPath = filename;
		setDllPath(dllPath);
		if (onDllSelected) onDllSelected(dllPath);
	}
}

void BuzzPluginView::onBrowseGearClicked()
{
	if (scanning) return;

	char folderPath[MAX_PATH] = {};

	BROWSEINFOA bi = {};
	bi.hwndOwner = hwndContainer;
	bi.pszDisplayName = folderPath;
	bi.lpszTitle = "Select Buzz Gear Folder";
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

	PIDLIST_ABSOLUTE pidl = SHBrowseForFolderA(&bi);
	if (pidl) {
		if (SHGetPathFromIDListA(pidl, folderPath)) {
			gearDir = folderPath;
			setGearDir(gearDir);
			showScanningIndicator();

			if (scanThread.joinable())
				scanThread.join();

			scanning = true;
			std::string dirCopy = gearDir;
			HWND hwnd = hwndContainer;

			scanThread = std::thread([this, dirCopy, hwnd]() {
				if (onGearDirSelected) {
					onGearDirSelected(dirCopy);
				}
				scanning = false;
				if (IsWindow(hwnd)) {
					PostMessage(hwnd, WM_SCAN_COMPLETE, 0, 0);
				}
			});
		}
		CoTaskMemFree(pidl);
	}
}

void BuzzPluginView::onLoadSamplesClicked()
{
	// Multi-select file dialog for WAV files
	// GetOpenFileNameA with OFN_ALLOWMULTISELECT uses a double-null-terminated buffer:
	// "directory\0file1\0file2\0\0" or "fullpath\0\0" for single file
	char buffer[32768] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwndContainer;
	ofn.lpstrFilter = "WAV Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
	ofn.lpstrFile = buffer;
	ofn.nMaxFile = sizeof(buffer);
	ofn.lpstrTitle = "Select WAV Samples for Wave Table";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
	          | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

	if (!GetOpenFileNameA(&ofn))
		return;

	// Parse the multi-select result
	std::vector<std::string> paths;
	const char* p = buffer;

	// First string is the directory (if multiple) or full path (if single)
	std::string first = p;
	p += first.size() + 1;

	if (*p == '\0') {
		// Single file selected
		paths.push_back(first);
	} else {
		// Multiple files: first is directory, rest are filenames
		while (*p != '\0') {
			std::string filename = p;
			paths.push_back(first + "\\" + filename);
			p += filename.size() + 1;
		}
	}

	if (!paths.empty() && onSamplesSelected) {
		onSamplesSelected(paths);
	}
}

void BuzzPluginView::setWaveSlots(const std::vector<std::string>& slotNames)
{
	waveSlotNames = slotNames;
	populateWaveList();

	// Update the summary label
	int loaded = 0;
	for (auto& n : slotNames) {
		if (!n.empty()) loaded++;
	}
	if (hwndSamplesLabel) {
		char buf[128];
		if (loaded == 0) {
			snprintf(buf, sizeof(buf), "Wave table: empty");
		} else {
			snprintf(buf, sizeof(buf), "Wave table: %d sample%s", loaded, loaded == 1 ? "" : "s");
		}
		SetWindowTextA(hwndSamplesLabel, buf);
	}
}

void BuzzPluginView::populateWaveList()
{
	if (!hwndWaveList) return;

	SendMessage(hwndWaveList, LB_RESETCONTENT, 0, 0);

	// Show up to kVisibleWaveSlots slots (or more if loaded beyond that)
	int maxSlot = kVisibleWaveSlots;
	for (int i = maxSlot; i < (int)waveSlotNames.size(); i++) {
		if (!waveSlotNames[i].empty()) maxSlot = i + 1;
	}

	for (int i = 0; i < maxSlot; i++) {
		char display[256];
		if (i < (int)waveSlotNames.size() && !waveSlotNames[i].empty()) {
			snprintf(display, sizeof(display), "%02d: %s", i + 1, waveSlotNames[i].c_str());
		} else {
			snprintf(display, sizeof(display), "%02d: (empty)", i + 1);
		}
		int idx = (int)SendMessageA(hwndWaveList, LB_ADDSTRING, 0, (LPARAM)display);
		if (idx != LB_ERR) {
			// Store 1-based slot index
			SendMessage(hwndWaveList, LB_SETITEMDATA, idx, (LPARAM)(i + 1));
		}
	}
}

void BuzzPluginView::onWaveListDoubleClick()
{
	if (!hwndWaveList) return;

	int sel = (int)SendMessage(hwndWaveList, LB_GETCURSEL, 0, 0);
	if (sel == LB_ERR) return;

	LRESULT data = SendMessage(hwndWaveList, LB_GETITEMDATA, sel, 0);
	if (data == LB_ERR || data == 0) return;

	int slotIndex = (int)data; // 1-based

	// Open file dialog to select a WAV for this slot
	char filename[MAX_PATH] = {};

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwndContainer;
	ofn.lpstrFilter = "WAV Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;

	bool isEmpty = (slotIndex - 1 >= (int)waveSlotNames.size()) ||
	               waveSlotNames[slotIndex - 1].empty();

	char title[128];
	if (isEmpty) {
		snprintf(title, sizeof(title), "Insert sample into slot %d", slotIndex);
	} else {
		snprintf(title, sizeof(title), "Replace sample in slot %d", slotIndex);
	}
	ofn.lpstrTitle = title;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn)) {
		if (onSampleSlotChanged) {
			onSampleSlotChanged(slotIndex, filename);
		}
	}
}

void BuzzPluginView::setTrackInfo(int current, int min, int max)
{
	currentTracks = current;
	minTracks = min;
	maxTracks = max;

	if (hwndTrackLabel) {
		char buf[64];
		if (max > 0) {
			snprintf(buf, sizeof(buf), "Tracks: %d / %d", current, max);
		} else {
			snprintf(buf, sizeof(buf), "Tracks: N/A");
		}
		SetWindowTextA(hwndTrackLabel, buf);
	}

	// Enable/disable buttons
	if (hwndAddTrackButton)
		EnableWindow(hwndAddTrackButton, current < max && max > 0);
	if (hwndRemoveTrackButton)
		EnableWindow(hwndRemoveTrackButton, current > min && min >= 0);
}

void BuzzPluginView::showScanningIndicator()
{
	if (hwndMachineList) {
		SendMessage(hwndMachineList, LB_RESETCONTENT, 0, 0);
		SendMessageA(hwndMachineList, LB_ADDSTRING, 0, (LPARAM)"Scanning...");
	}
	if (hwndGearButton) {
		EnableWindow(hwndGearButton, FALSE);
	}
	if (hwndGearLabel) {
		std::string text = "Scanning: " + gearDir;
		SetWindowTextA(hwndGearLabel, text.c_str());
	}
}

void BuzzPluginView::onScanComplete()
{
	if (hwndGearButton) {
		EnableWindow(hwndGearButton, TRUE);
	}
	setGearDir(gearDir);
	populateMachineList();
}

void BuzzPluginView::onMachineListDoubleClick()
{
	if (!hwndMachineList || scanning) return;

	int sel = (int)SendMessage(hwndMachineList, LB_GETCURSEL, 0, 0);
	if (sel == LB_ERR) return;

	LRESULT data = SendMessage(hwndMachineList, LB_GETITEMDATA, sel, 0);
	if (data == LB_ERR || data == 0) return;

	int entryIndex = (int)data - 1;
	if (entryIndex >= 0 && entryIndex < (int)gearEntries.size()) {
		const auto& entry = gearEntries[entryIndex];
		dllPath = entry.dllPath;
		setDllPath(dllPath);
		setMachineName(entry.displayName);
		if (onDllSelected) onDllSelected(dllPath);
	}
}

void BuzzPluginView::populateMachineList()
{
	if (!hwndMachineList) return;

	SendMessage(hwndMachineList, LB_RESETCONTENT, 0, 0);

	int filterType = isGenerator ? MT_GENERATOR : MT_EFFECT;
	int count = 0;

	// Build lowercase filter for case-insensitive matching
	std::string filterLower = machineFilter;
	for (auto& c : filterLower) c = (char)tolower((unsigned char)c);

	for (int i = 0; i < (int)gearEntries.size(); i++) {
		const auto& entry = gearEntries[i];
		if (entry.machineType != filterType) continue;

		std::string display;
		if (!entry.category.empty())
			display = entry.category + "/" + entry.displayName;
		else
			display = entry.displayName;

		// Apply text filter (case-insensitive)
		if (!filterLower.empty()) {
			std::string displayLower = display;
			for (auto& c : displayLower) c = (char)tolower((unsigned char)c);
			if (displayLower.find(filterLower) == std::string::npos) continue;
		}

		int idx = (int)SendMessageA(hwndMachineList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
		if (idx != LB_ERR) {
			SendMessage(hwndMachineList, LB_SETITEMDATA, idx, (LPARAM)(i + 1));
			count++;
		}
	}

	if (!gearDir.empty() && hwndGearLabel) {
		char buf[512];
		snprintf(buf, sizeof(buf), "Gear: %s (%d machines)", gearDir.c_str(), count);
		SetWindowTextA(hwndGearLabel, buf);
	}
}

void BuzzPluginView::setMachineName(const std::string& name)
{
	machineName = name;
	if (hwndLabel)
		SetWindowTextA(hwndLabel, name.empty() ? "No machine loaded" : name.c_str());
}

void BuzzPluginView::setDllPath(const std::string& path)
{
	dllPath = path;
	if (hwndPathLabel)
		SetWindowTextA(hwndPathLabel, path.c_str());
}

void BuzzPluginView::setGearDir(const std::string& dir)
{
	gearDir = dir;
	if (hwndGearLabel) {
		std::string text = dir.empty() ? "Gear folder: (not set)" : "Gear: " + dir;
		SetWindowTextA(hwndGearLabel, text.c_str());
	}
}

void BuzzPluginView::setGearEntries(const std::vector<GearEntry>& entries)
{
	gearEntries = entries;
	populateMachineList();
}

LRESULT CALLBACK BuzzPluginView::ParamPanelWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_CREATE) {
		auto* cs = (CREATESTRUCT*)lParam;
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
		return 0;
	}

	auto* self = (BuzzPluginView*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

	switch (msg) {
		case WM_VSCROLL: {
			if (!self) break;
			SCROLLINFO si = {};
			si.cbSize = sizeof(si);
			si.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &si);

			int oldPos = si.nPos;
			switch (LOWORD(wParam)) {
				case SB_LINEUP:    si.nPos -= self->S(22); break;
				case SB_LINEDOWN:  si.nPos += self->S(22); break;
				case SB_PAGEUP:    si.nPos -= si.nPage; break;
				case SB_PAGEDOWN:  si.nPos += si.nPage; break;
				case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
			}

			si.fMask = SIF_POS;
			SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
			GetScrollInfo(hWnd, SB_VERT, &si); // get clamped value

			if (si.nPos != oldPos) {
				ScrollWindowEx(hWnd, 0, oldPos - si.nPos, nullptr, nullptr, nullptr, nullptr,
					SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
				self->paramScrollPos = si.nPos;
			}
			return 0;
		}

		case WM_HSCROLL: {
			// Trackbar notifications come as WM_HSCROLL
			if (!self) break;
			HWND hwndTrackbar = (HWND)lParam;
			if (!hwndTrackbar) break;

			int ctrlId = GetDlgCtrlID(hwndTrackbar);
			int paramIndex = ctrlId - kParamSliderBaseID;
			if (paramIndex < 0 || paramIndex >= (int)self->paramControls.size()) break;

			auto& pc = self->paramControls[paramIndex];
			int pos = (int)SendMessage(hwndTrackbar, TBM_GETPOS, 0, 0);
			int rangeMax = (int)SendMessage(hwndTrackbar, TBM_GETRANGEMAX, 0, 0);
			double normalized = (rangeMax > 0) ? (double)pos / (double)rangeMax : 0.0;

			// Update value label
			if (pc.hwndValueLabel) {
				int rawValue = pc.minValue + pos;
				int descIdx = rawValue - pc.minValue;
				if (descIdx >= 0 && descIdx < (int)pc.valueDescriptions.size() &&
				    !pc.valueDescriptions[descIdx].empty()) {
					std::wstring wdesc(pc.valueDescriptions[descIdx].begin(),
					                   pc.valueDescriptions[descIdx].end());
					SetWindowTextW(pc.hwndValueLabel, wdesc.c_str());
				} else {
					wchar_t valBuf[32];
					swprintf(valBuf, 32, L"%d", rawValue);
					SetWindowTextW(pc.hwndValueLabel, valBuf);
				}
			}

			int code = LOWORD(wParam);
			if (code == TB_THUMBTRACK || code == TB_THUMBPOSITION) {
				if (self->onParamBeginEdit)
					self->onParamBeginEdit(pc.paramId);
				if (self->onParamChanged)
					self->onParamChanged(pc.paramId, normalized);
			} else if (code == TB_ENDTRACK) {
				// Final position
				if (self->onParamChanged)
					self->onParamChanged(pc.paramId, normalized);
				if (self->onParamEndEdit)
					self->onParamEndEdit(pc.paramId);
			} else {
				// Other scroll codes (line/page)
				if (self->onParamBeginEdit)
					self->onParamBeginEdit(pc.paramId);
				if (self->onParamChanged)
					self->onParamChanged(pc.paramId, normalized);
				if (self->onParamEndEdit)
					self->onParamEndEdit(pc.paramId);
			}
			return 0;
		}

		case WM_MOUSEWHEEL: {
			// Handle mouse wheel scrolling (no modal loop, doesn't block audio)
			if (!self) break;
			int delta = GET_WHEEL_DELTA_WPARAM(wParam);
			SCROLLINFO si = {};
			si.cbSize = sizeof(si);
			si.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &si);

			int oldPos = si.nPos;
			si.nPos -= delta / 2; // scroll speed

			si.fMask = SIF_POS;
			SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
			GetScrollInfo(hWnd, SB_VERT, &si);

			if (si.nPos != oldPos) {
				ScrollWindowEx(hWnd, 0, oldPos - si.nPos, nullptr, nullptr, nullptr, nullptr,
					SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
				self->paramScrollPos = si.nPos;
			}
			return 0;
		}

		case WM_ERASEBKGND: {
			HDC hdc = (HDC)wParam;
			RECT rc;
			GetClientRect(hWnd, &rc);
			FillRect(hdc, &rc, (HBRUSH)(COLOR_3DFACE + 1));
			return 1;
		}
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void BuzzPluginView::createParamControls()
{
	if (!hwndParamPanel) return;

	HINSTANCE hInst = GetModuleHandle(nullptr);
	int rowHeight = S(22);
	int labelWidth = S(120);
	int margin = S(4);

	RECT panelRect;
	GetClientRect(hwndParamPanel, &panelRect);
	int panelWidth = panelRect.right - panelRect.left;
	int sliderWidth = panelWidth - labelWidth - margin * 3;
	if (sliderWidth < S(50)) sliderWidth = S(50);

	paramScrollPos = 0;

	int valueLabelWidth = S(70);
	int adjustedSliderWidth = sliderWidth - valueLabelWidth - margin;
	if (adjustedSliderWidth < S(50)) adjustedSliderWidth = S(50);

	for (int i = 0; i < (int)paramInfos.size(); i++) {
		const auto& pi = paramInfos[i];
		ParamControl pc;
		pc.paramId = pi.paramId;
		pc.stepCount = pi.stepCount;
		pc.minValue = pi.minValue;
		pc.valueDescriptions = pi.valueDescriptions;

		int y = i * rowHeight;

		// Create label
		std::wstring wname(pi.name.begin(), pi.name.end());
		pc.hwndLabel = CreateWindowExW(0, L"STATIC", wname.c_str(),
			WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
			margin, y + S(2), labelWidth, S(16),
			hwndParamPanel, (HMENU)(INT_PTR)(kParamLabelBaseID + i), hInst, nullptr);
		SendMessage(pc.hwndLabel, WM_SETFONT, (WPARAM)hSmallFont, TRUE);

		// Create trackbar
		pc.hwndTrackbar = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
			WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
			labelWidth + margin * 2, y, adjustedSliderWidth, rowHeight,
			hwndParamPanel, (HMENU)(INT_PTR)(kParamSliderBaseID + i), hInst, nullptr);

		int rangeMax = (pi.stepCount > 0) ? pi.stepCount : 1000;
		SendMessage(pc.hwndTrackbar, TBM_SETRANGEMIN, FALSE, 0);
		SendMessage(pc.hwndTrackbar, TBM_SETRANGEMAX, FALSE, rangeMax);
		int pos = (int)(pi.normalizedValue * rangeMax + 0.5);
		SendMessage(pc.hwndTrackbar, TBM_SETPOS, TRUE, pos);

		// Create value label (shows numeric value or description string)
		int valueLabelX = labelWidth + margin * 2 + adjustedSliderWidth + margin;
		pc.hwndValueLabel = CreateWindowExW(0, L"STATIC", L"",
			WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
			valueLabelX, y + S(2), valueLabelWidth, S(16),
			hwndParamPanel, nullptr, hInst, nullptr);
		SendMessage(pc.hwndValueLabel, WM_SETFONT, (WPARAM)hSmallFont, TRUE);

		// Set initial value text
		int rawValue = pc.minValue + pos;
		int descIdx = rawValue - pc.minValue;
		if (descIdx >= 0 && descIdx < (int)pc.valueDescriptions.size() &&
		    !pc.valueDescriptions[descIdx].empty()) {
			std::wstring wdesc(pc.valueDescriptions[descIdx].begin(),
			                   pc.valueDescriptions[descIdx].end());
			SetWindowTextW(pc.hwndValueLabel, wdesc.c_str());
		} else {
			wchar_t valBuf[32];
			swprintf(valBuf, 32, L"%d", rawValue);
			SetWindowTextW(pc.hwndValueLabel, valBuf);
		}

		paramControls.push_back(pc);
	}

	// Set scroll range
	int totalHeight = (int)paramInfos.size() * rowHeight;
	int panelHeight = panelRect.bottom - panelRect.top;

	SCROLLINFO si = {};
	si.cbSize = sizeof(si);
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	si.nMin = 0;
	si.nMax = totalHeight > 0 ? totalHeight - 1 : 0;
	si.nPage = panelHeight;
	si.nPos = 0;
	SetScrollInfo(hwndParamPanel, SB_VERT, &si, TRUE);
}

void BuzzPluginView::destroyParamControls()
{
	for (auto& pc : paramControls) {
		if (pc.hwndLabel) DestroyWindow(pc.hwndLabel);
		if (pc.hwndTrackbar) DestroyWindow(pc.hwndTrackbar);
		if (pc.hwndValueLabel) DestroyWindow(pc.hwndValueLabel);
	}
	paramControls.clear();
	paramScrollPos = 0;
}

void BuzzPluginView::setParamInfo(const std::vector<ParamViewInfo>& params)
{
	paramInfos = params;
	destroyParamControls();
	createParamControls();
}

void BuzzPluginView::updateParamValue(Steinberg::Vst::ParamID id, double normalizedValue)
{
	updatingFromHost = true;
	for (auto& pc : paramControls) {
		if (pc.paramId == id) {
			int rangeMax = (int)SendMessage(pc.hwndTrackbar, TBM_GETRANGEMAX, 0, 0);
			int pos = (int)(normalizedValue * rangeMax + 0.5);
			SendMessage(pc.hwndTrackbar, TBM_SETPOS, TRUE, pos);

			// Update value label
			if (pc.hwndValueLabel) {
				int rawValue = pc.minValue + pos;
				int descIdx = rawValue - pc.minValue;
				if (descIdx >= 0 && descIdx < (int)pc.valueDescriptions.size() &&
				    !pc.valueDescriptions[descIdx].empty()) {
					std::wstring wdesc(pc.valueDescriptions[descIdx].begin(),
					                   pc.valueDescriptions[descIdx].end());
					SetWindowTextW(pc.hwndValueLabel, wdesc.c_str());
				} else {
					wchar_t valBuf[32];
					swprintf(valBuf, 32, L"%d", rawValue);
					SetWindowTextW(pc.hwndValueLabel, valBuf);
				}
			}
			break;
		}
	}
	// Also update the cached value in paramInfos
	for (auto& pi : paramInfos) {
		if (pi.paramId == id) {
			pi.normalizedValue = normalizedValue;
			break;
		}
	}
	updatingFromHost = false;
}

void BuzzPluginView::setPresetNames(const std::vector<std::string>& names)
{
	if (!hwndPresetCombo) return;

	SendMessage(hwndPresetCombo, CB_RESETCONTENT, 0, 0);

	if (names.empty()) {
		ShowWindow(hwndPresetLabel, SW_HIDE);
		ShowWindow(hwndPresetCombo, SW_HIDE);
		if (hwndSavePresetButton) ShowWindow(hwndSavePresetButton, SW_SHOW);
		return;
	}

	// Add "(default)" as first item
	SendMessageW(hwndPresetCombo, CB_ADDSTRING, 0, (LPARAM)L"(default)");

	for (auto& name : names) {
		std::wstring wname(name.begin(), name.end());
		SendMessageW(hwndPresetCombo, CB_ADDSTRING, 0, (LPARAM)wname.c_str());
	}

	SendMessage(hwndPresetCombo, CB_SETCURSEL, 0, 0);
	ShowWindow(hwndPresetLabel, SW_SHOW);
	ShowWindow(hwndPresetCombo, SW_SHOW);
	if (hwndSavePresetButton) ShowWindow(hwndSavePresetButton, SW_SHOW);
}

} // namespace BuzzVst
