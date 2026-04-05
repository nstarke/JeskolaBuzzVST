#include "BuzzPresetLoader.h"
#include <fstream>
#include <cstring>

namespace BuzzVst {

static bool ReadU32(std::ifstream& f, uint32_t& val) {
    return !!f.read(reinterpret_cast<char*>(&val), 4);
}

static bool ReadString(std::ifstream& f, uint32_t len, std::string& out) {
    if (len == 0) { out.clear(); return true; }
    if (len > 4096) return false; // sanity limit
    out.resize(len);
    if (!f.read(&out[0], len)) return false;
    // Strip trailing nulls/whitespace
    while (!out.empty() && (out.back() == '\0' || out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return true;
}

static void WriteU32(std::ofstream& f, uint32_t val) {
    f.write(reinterpret_cast<const char*>(&val), 4);
}

static void WriteString(std::ofstream& f, const std::string& s) {
    f.write(s.c_str(), s.size());
}

bool BuzzPresetLoader::Load(const std::string& prsPath) {
    Clear();

    std::ifstream f(prsPath, std::ios::binary);
    if (!f.is_open()) return false;

    // Version
    uint32_t version = 0;
    if (!ReadU32(f, version)) return false;
    if (version != 1) return false;

    // Machine name
    uint32_t nameLen = 0;
    if (!ReadU32(f, nameLen)) return false;
    if (!ReadString(f, nameLen, machineName)) return false;

    // Number of presets
    uint32_t numPresets = 0;
    if (!ReadU32(f, numPresets)) return false;
    if (numPresets > 10000) return false; // sanity limit

    for (uint32_t i = 0; i < numPresets; i++) {
        BuzzPreset preset;

        // Preset name
        uint32_t pNameLen = 0;
        if (!ReadU32(f, pNameLen)) break;
        if (!ReadString(f, pNameLen, preset.name)) break;

        // Number of tracks (always 1 in known files)
        uint32_t numTracks = 0;
        if (!ReadU32(f, numTracks)) break;

        // Number of parameters
        uint32_t numParams = 0;
        if (!ReadU32(f, numParams)) break;
        if (numParams > 512) break; // sanity limit

        // Parameter values
        preset.paramValues.resize(numParams);
        for (uint32_t p = 0; p < numParams; p++) {
            uint32_t val = 0;
            if (!ReadU32(f, val)) break;
            preset.paramValues[p] = (int32_t)val;
        }
        if (preset.paramValues.size() != numParams) break;

        // Comment
        uint32_t commentLen = 0;
        if (!ReadU32(f, commentLen)) break;
        if (commentLen > 0) {
            if (!ReadString(f, commentLen, preset.comment)) break;
        }

        presets.push_back(std::move(preset));
    }

    return !presets.empty();
}

bool BuzzPresetLoader::Save(const std::string& prsPath) const {
    if (machineName.empty()) return false;

    std::ofstream f(prsPath, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;

    // Version
    WriteU32(f, 1);

    // Machine name
    WriteU32(f, (uint32_t)machineName.size());
    WriteString(f, machineName);

    // Number of presets
    WriteU32(f, (uint32_t)presets.size());

    for (auto& preset : presets) {
        // Preset name
        WriteU32(f, (uint32_t)preset.name.size());
        WriteString(f, preset.name);

        // Number of tracks (always 1)
        WriteU32(f, 1);

        // Number of parameters
        WriteU32(f, (uint32_t)preset.paramValues.size());

        // Parameter values
        for (auto val : preset.paramValues) {
            WriteU32(f, (uint32_t)val);
        }

        // Comment
        WriteU32(f, (uint32_t)preset.comment.size());
        if (!preset.comment.empty()) {
            WriteString(f, preset.comment);
        }
    }

    return f.good();
}

std::string BuzzPresetLoader::PrsPathForDll(const std::string& dllPath) {
    if (dllPath.size() < 4) return "";
    size_t dotPos = dllPath.rfind('.');
    if (dotPos == std::string::npos) return "";
    return dllPath.substr(0, dotPos) + ".prs";
}

std::string BuzzPresetLoader::FindPrsForDll(const std::string& dllPath) {
    std::string prsPath = PrsPathForDll(dllPath);
    if (prsPath.empty()) return "";
    std::ifstream f(prsPath);
    if (f.good()) return prsPath;
    return "";
}

} // namespace BuzzVst
