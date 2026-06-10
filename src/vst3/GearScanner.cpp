#include <windows.h>
#include "GearScanner.h"
#include "../buzz/MachineInterface.h"
#include "../common/SEHGuard.h"
#include "../common/PatchMessageBoxes.h"

#include <algorithm>
#include <cstring>

namespace BuzzVst {

typedef CMachineInfo const* (__cdecl *GetInfoFunc)();

bool GearScanner::Scan(const std::string& gearDir)
{
	// Neutralize MessageBox popups before probing any machine DLL — some
	// machines call MessageBox from DllMain/GetInfo (e.g. a "GetInfo() Called"
	// debug dialog) which would otherwise block the host's plugin scan.
	PatchMessageBoxesOnce();

	entries.clear();

	if (gearDir.empty()) return false;

	DWORD attrs = GetFileAttributesA(gearDir.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
		return false;

	rootDir = gearDir;

	while (!rootDir.empty() && (rootDir.back() == '\\' || rootDir.back() == '/'))
		rootDir.pop_back();

	scanDirectory(rootDir, "");

	std::sort(entries.begin(), entries.end(), [](const GearEntry& a, const GearEntry& b) {
		if (a.category != b.category) return a.category < b.category;
		return a.displayName < b.displayName;
	});

	return true;
}

void GearScanner::scanDirectory(const std::string& dir, const std::string& relPath)
{
	std::string searchPath = dir + "\\*";

	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
	if (hFind == INVALID_HANDLE_VALUE) return;

	do {
		if (fd.cFileName[0] == '.' &&
			(fd.cFileName[1] == 0 || (fd.cFileName[1] == '.' && fd.cFileName[2] == 0)))
			continue;

		std::string fullPath = dir + "\\" + fd.cFileName;

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			std::string childRel = relPath.empty() ? fd.cFileName : relPath + "/" + fd.cFileName;
			scanDirectory(fullPath, childRel);
		} else {
			const char* ext = strrchr(fd.cFileName, '.');
			if (ext && (_stricmp(ext, ".dll") == 0)) {
				probeOneDll(fullPath, relPath);
			}
		}
	} while (FindNextFileA(hFind, &fd));

	FindClose(hFind);
}

// Infer machine type from directory name (case-insensitive).
// Returns MT_GENERATOR, MT_EFFECT, or -1 if unknown.
static int InferTypeFromCategory(const std::string& category)
{
	// Check each path component for generator/effect keywords
	std::string lower = category;
	for (auto& c : lower) c = (char)tolower((unsigned char)c);

	if (lower.find("generator") != std::string::npos) return MT_GENERATOR;
	if (lower.find("effect") != std::string::npos) return MT_EFFECT;
	return -1;
}

// Extract display name from a DLL filename (strip path and extension).
static std::string NameFromFilename(const std::string& dllPath)
{
	const char* fname = strrchr(dllPath.c_str(), '\\');
	if (!fname) fname = strrchr(dllPath.c_str(), '/');
	if (fname) fname++; else fname = dllPath.c_str();
	std::string name = fname;
	auto dotPos = name.rfind('.');
	if (dotPos != std::string::npos)
		name = name.substr(0, dotPos);
	return name;
}

bool GearScanner::probeOneDll(const std::string& dllPath, const std::string& category)
{
	// Step 1: Try to load the DLL without running DllMain to check for GetInfo export.
	// This will fail for 32-bit DLLs in a 64-bit process, which is expected.
	HMODULE hCheck = LoadLibraryExA(dllPath.c_str(), nullptr,
		DONT_RESOLVE_DLL_REFERENCES);

	if (!hCheck) {
		// Can't load the DLL (likely 32-bit DLL in 64-bit process).
		// Fall back to inferring type from directory name and name from filename.
		int inferredType = InferTypeFromCategory(category);
		if (inferredType < 0) return false; // Can't determine type

		GearEntry entry;
		entry.dllPath = dllPath;
		entry.category = category;
		entry.machineType = inferredType;
		entry.displayName = NameFromFilename(dllPath);
		if (entry.displayName.empty()) return false;

		entries.push_back(std::move(entry));
		return true;
	}

	bool hasExport = (GetProcAddress(hCheck, "GetInfo") != nullptr);
	FreeLibrary(hCheck);

	if (!hasExport) return false;

	// Step 2: Full load with DllMain — wrap in SEH because old Buzz machines
	// can crash during initialization.
	HMODULE hDll = nullptr;
	bool loadOk = SEH_Call([&]() {
		hDll = LoadLibraryA(dllPath.c_str());
	});

	if (!loadOk || !hDll) return false;

	// Step 3: Get the function pointer and call it, all under SEH
	auto fnGetInfo = (GetInfoFunc)GetProcAddress(hDll, "GetInfo");
	if (!fnGetInfo) {
		SEH_Call([&]() { FreeLibrary(hDll); });
		return false;
	}

	const CMachineInfo* pInfo = nullptr;
	bool infoOk = SEH_Call([&]() {
		pInfo = fnGetInfo();
	});

	if (!infoOk || !pInfo) {
		SEH_Call([&]() { FreeLibrary(hDll); });
		return false;
	}

	// Step 4: Read machine info under SEH
	GearEntry entry;
	entry.dllPath = dllPath;
	entry.category = category;
	entry.machineType = -1;

	bool readOk = SEH_Call([&]() {
		if (pInfo->Type != MT_GENERATOR && pInfo->Type != MT_EFFECT)
			return;

		entry.machineType = pInfo->Type;
		entry.flags = pInfo->Flags;

		if (pInfo->Name && pInfo->Name[0]) {
			char nameBuf[256] = {};
			strncpy(nameBuf, pInfo->Name, sizeof(nameBuf) - 1);
			entry.displayName = nameBuf;
		}
	});

	SEH_Call([&]() { FreeLibrary(hDll); });

	if (!readOk || entry.machineType < 0) return false;

	// Fallback display name from filename
	if (entry.displayName.empty()) {
		const char* fname = strrchr(dllPath.c_str(), '\\');
		if (!fname) fname = strrchr(dllPath.c_str(), '/');
		if (fname) fname++; else fname = dllPath.c_str();
		entry.displayName = fname;
		auto dotPos = entry.displayName.rfind('.');
		if (dotPos != std::string::npos)
			entry.displayName = entry.displayName.substr(0, dotPos);
	}

	entries.push_back(std::move(entry));
	return true;
}

std::vector<GearEntry> GearScanner::GetGenerators() const
{
	std::vector<GearEntry> result;
	for (auto& e : entries) {
		if (e.machineType == MT_GENERATOR)
			result.push_back(e);
	}
	return result;
}

std::vector<GearEntry> GearScanner::GetEffects() const
{
	std::vector<GearEntry> result;
	for (auto& e : entries) {
		if (e.machineType == MT_EFFECT)
			result.push_back(e);
	}
	return result;
}

} // namespace BuzzVst
