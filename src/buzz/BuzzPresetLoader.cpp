#include "BuzzPresetLoader.h"
#include <windows.h>
#include <fstream>
#include <cstring>
#include <cctype>

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
    // Strip embedded control chars (except space) that could indicate corruption
    for (auto& c : out) {
        if (c > 0 && c < 0x20 && c != '\t') c = ' ';
    }
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

    // Check file size to prevent excessive allocation from malicious files
    f.seekg(0, std::ios::end);
    auto fileSize = f.tellg();
    f.seekg(0, std::ios::beg);
    if (fileSize <= 0 || fileSize > 10 * 1024 * 1024) return false; // max 10 MB

    // Version
    uint32_t version = 0;
    if (!ReadU32(f, version)) return false;
    if (version != 1) return false;

    // Machine name
    uint32_t nameLen = 0;
    if (!ReadU32(f, nameLen)) return false;
    if (nameLen > 256) return false; // machine names should be short
    if (!ReadString(f, nameLen, machineName)) return false;
    if (machineName.empty()) return false;

    // Number of presets
    uint32_t numPresets = 0;
    if (!ReadU32(f, numPresets)) return false;
    if (numPresets > 10000) return false; // sanity limit

    for (uint32_t i = 0; i < numPresets; i++) {
        BuzzPreset preset;

        // Preset name
        uint32_t pNameLen = 0;
        if (!ReadU32(f, pNameLen)) break;
        if (pNameLen > 256) break; // preset names should be short
        if (!ReadString(f, pNameLen, preset.name)) break;
        if (preset.name.empty()) break;

        // Number of tracks (always 1 in known files)
        uint32_t numTracks = 0;
        if (!ReadU32(f, numTracks)) break;
        if (numTracks > 64) break; // sanity limit

        // Number of parameters
        uint32_t numParams = 0;
        if (!ReadU32(f, numParams)) break;
        if (numParams > 512) break; // sanity limit

        // Parameter values — read one at a time with failure tracking
        preset.paramValues.reserve(numParams);
        bool readOk = true;
        for (uint32_t p = 0; p < numParams; p++) {
            uint32_t val = 0;
            if (!ReadU32(f, val)) { readOk = false; break; }
            preset.paramValues.push_back((int32_t)val);
        }
        if (!readOk) break;

        // Comment
        uint32_t commentLen = 0;
        if (!ReadU32(f, commentLen)) break;
        if (commentLen > 4096) break; // sanity limit for comments
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

static bool DirExists(const std::string& path) {
    if (path.empty()) return false;
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string GetEnv(const char* name) {
    char buf[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA(name, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "";
    return std::string(buf);
}

std::string BuzzPresetLoader::ResolveBackupRoot() {
    // Explicit override (works on any platform, including WINE).
    std::string envDir = GetEnv("BUZZVST_BACKUP_DIR");
    if (DirExists(envDir)) return envDir;

    // Legacy fallback: %USERPROFILE%\OneDrive\BuzzVST (author's personal
    // backup location on Windows). Under WINE OneDrive typically isn't
    // present, so this returns empty and backups are silently skipped.
    std::string profile = GetEnv("USERPROFILE");
    if (profile.empty()) return "";
    std::string oneDrive = profile + "\\OneDrive";
    if (!DirExists(oneDrive)) return "";
    return oneDrive + "\\BuzzVST";
}

// Build the backup .prs path for a DLL. Given a DLL like
// "C:\Users\X\Buzz\Gear\generators\Machine.dll", extracts the relative
// "\Gear\generators\Machine.prs" portion and maps it under the backup root
// returned by ResolveBackupRoot(). Returns empty string if backups are not
// configured or the DLL isn't under a Gear directory.
static std::string backupPrsPath(const std::string& dllPath) {
    std::string prs = BuzzPresetLoader::PrsPathForDll(dllPath);
    if (prs.empty()) return "";

    // Find the \Gear\ segment (case-insensitive)
    std::string lower = prs;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    size_t gearPos = lower.find("\\gear\\");
    if (gearPos == std::string::npos) gearPos = lower.find("/gear/");
    if (gearPos == std::string::npos) return "";

    std::string backupRoot = BuzzPresetLoader::ResolveBackupRoot();
    if (backupRoot.empty()) return "";

    std::string relPath = prs.substr(gearPos); // e.g. \Gear\generators\Machine.prs
    return backupRoot + relPath;
}

std::string BuzzPresetLoader::FindPrsForDll(const std::string& dllPath) {
    // 1. Try backup location first (BUZZVST_BACKUP_DIR / OneDrive mirror)
    std::string odPath = backupPrsPath(dllPath);
    if (!odPath.empty()) {
        std::ifstream f(odPath);
        if (f.good()) return odPath;
    }

    // 2. Fall back to local .prs beside the DLL
    std::string prsPath = PrsPathForDll(dllPath);
    if (prsPath.empty()) return "";
    std::ifstream f(prsPath);
    if (f.good()) return prsPath;
    return "";
}

} // namespace BuzzVst
