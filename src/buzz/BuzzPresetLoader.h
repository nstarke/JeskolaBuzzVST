#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace BuzzVst {

struct BuzzPreset {
    std::string name;
    std::string comment;
    std::vector<int32_t> paramValues; // raw Buzz param values (state params only)
};

// Loads Buzz preset (.prs) files.
// Format: version(u32), machineNameLen(u32), machineName(chars),
//   numPresets(u32), then for each preset:
//   nameLen(u32), name(chars), numTracks(u32), numParams(u32),
//   params(u32[numParams]), commentLen(u32), comment(chars)
class BuzzPresetLoader {
public:
    // Load presets from a .prs file. Returns true if any presets were loaded.
    bool Load(const std::string& prsPath);

    // Save all presets (including any added) back to a .prs file.
    // Creates the file if it doesn't exist.
    bool Save(const std::string& prsPath) const;

    // Add a preset to the in-memory list.
    void AddPreset(const BuzzPreset& preset) { presets.push_back(preset); }

    // Find the .prs file for a machine DLL (same name, .prs extension)
    static std::string PrsPathForDll(const std::string& dllPath);

    // Find existing .prs file (returns empty if not found)
    static std::string FindPrsForDll(const std::string& dllPath);

    // Resolve the backup root directory for preset mirrors, or empty string
    // if backups are not configured. Checks BUZZVST_BACKUP_DIR env override
    // first, then falls back to %USERPROFILE%\OneDrive\BuzzVST if OneDrive
    // is present. Under WINE without OneDrive and without the env var set,
    // returns empty (backups are silently disabled).
    static std::string ResolveBackupRoot();

    void SetMachineName(const std::string& name) { machineName = name; }
    const std::string& GetMachineName() const { return machineName; }
    const std::vector<BuzzPreset>& GetPresets() const { return presets; }
    void Clear() { machineName.clear(); presets.clear(); }

private:
    std::string machineName;
    std::vector<BuzzPreset> presets;
};

} // namespace BuzzVst
