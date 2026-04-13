#pragma once

#include <string>
#include <vector>

namespace BuzzVst {

struct GearEntry {
	std::string displayName;  // Machine name from CMachineInfo::Name
	std::string dllPath;      // Full absolute path to the DLL
	std::string category;     // Relative subdirectory from gear root (e.g. "Generators")
	int machineType;          // MT_GENERATOR or MT_EFFECT
	int flags = 0;            // CMachineInfo::Flags (MIF_*). 0 when unread (64-bit fallback).
};

// Scans a Gear directory tree for Buzz machine DLLs.
// For each *.dll found, probes it by calling GetInfo() to determine
// the machine type and display name. Non-machine DLLs are skipped.
class GearScanner {
public:
	// Recursively scan gearDir for *.dll files and probe each.
	// Returns true if the directory was accessible (even if no machines found).
	bool Scan(const std::string& gearDir);

	// All discovered machines.
	const std::vector<GearEntry>& GetEntries() const { return entries; }

	// Filtered views.
	std::vector<GearEntry> GetGenerators() const;
	std::vector<GearEntry> GetEffects() const;

	// Clear results.
	void Clear() { entries.clear(); }

private:
	void scanDirectory(const std::string& dir, const std::string& relPath);
	bool probeOneDll(const std::string& dllPath, const std::string& category);

	std::string rootDir;
	std::vector<GearEntry> entries;
};

} // namespace BuzzVst
