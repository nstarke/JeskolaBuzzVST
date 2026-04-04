#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include "BuzzPluginView.h"
#include "../buzz/MachineInterface.h"
#include "pluginterfaces/gui/iplugview.h"

#include <cstdio>

namespace BuzzVst {

using namespace Steinberg;

static const wchar_t* kWindowClassName = L"BuzzPluginViewClass";
static bool sClassRegistered = false;

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
	size->left = 0;
	size->top = 0;
	size->right = S(kBaseWidth);
	size->bottom = S(kBaseHeight);
	return kResultOk;
}

tresult PLUGIN_API BuzzPluginView::setContentScaleFactor(ScaleFactor factor)
{
	if (factor < 0.5f) factor = 0.5f;
	if (factor > 4.0f) factor = 4.0f;

	if (factor == scaleFactor)
		return kResultTrue;

	scaleFactor = factor;

	// Update the rect
	ViewRect r(0, 0, S(kBaseWidth), S(kBaseHeight));
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
		hwndMachineList = nullptr;
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

	// Destroy old controls
	destroyFonts();
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
}

void BuzzPluginView::createControls(HWND parent)
{
	HINSTANCE hInst = GetModuleHandle(nullptr);

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

	createFonts();

	int w = S(kBaseWidth);
	int h = S(kBaseHeight);

	hwndContainer = CreateWindowExW(
		0, kWindowClassName, L"",
		WS_CHILD | WS_VISIBLE,
		0, 0, w, h,
		parent, nullptr, hInst, this
	);

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
	y += S(22);

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

	// Gear directory label
	hwndGearLabel = CreateWindowExW(0, L"STATIC", L"Gear folder: (not set)",
		WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
		innerMargin, y, w - 2 * innerMargin, S(14), hwndContainer, nullptr, hInst, nullptr);
	SendMessage(hwndGearLabel, WM_SETFONT, (WPARAM)hSmallFont, TRUE);
	y += S(18);

	// Machine listbox
	hwndMachineList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
		WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
		innerMargin, y, w - 2 * innerMargin, h - y - S(8),
		hwndContainer, (HMENU)(INT_PTR)kMachineListID, hInst, nullptr);
	SendMessage(hwndMachineList, WM_SETFONT, (WPARAM)hFont, TRUE);

	// Restore initial state
	if (!machineName.empty()) setMachineName(machineName);
	if (!dllPath.empty()) setDllPath(dllPath);
	if (!gearDir.empty()) setGearDir(gearDir);
	if (!gearEntries.empty()) populateMachineList();
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

	for (int i = 0; i < (int)gearEntries.size(); i++) {
		const auto& entry = gearEntries[i];
		if (entry.machineType != filterType) continue;

		std::string display;
		if (!entry.category.empty())
			display = entry.category + "/" + entry.displayName;
		else
			display = entry.displayName;

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

} // namespace BuzzVst
