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

    // Find the .prs file for a machine DLL (same name, .prs extension)
    static std::string FindPrsForDll(const std::string& dllPath);

    const std::string& GetMachineName() const { return machineName; }
    const std::vector<BuzzPreset>& GetPresets() const { return presets; }
    void Clear() { machineName.clear(); presets.clear(); }

private:
    std::string machineName;
    std::vector<BuzzPreset> presets;
};

} // namespace BuzzVst
