#pragma once

// Shared test helper functions used across multiple test files.

#include <windows.h>
#include <string>
#include "../src/buzz/MachineInterface.h"

// Create a CMachineParameter for testing.
inline CMachineParameter MakeParam(CMPType type, int minVal, int maxVal,
                                    int noVal, int defVal, int flags = MPF_STATE) {
    CMachineParameter p = {};
    p.Type = type;
    p.MinValue = minVal;
    p.MaxValue = maxVal;
    p.NoValue = noVal;
    p.DefValue = defVal;
    p.Flags = flags;
    p.Name = "Test";
    return p;
}

// Resolve a path relative to the project root (for ref/ test data).
// Walks up 4 directories from the exe (build32/bin/Release/exe → project root).
inline std::string GetRefPath(const char* relPath) {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string path = exePath;
    for (int i = 0; i < 4; i++) {
        size_t pos = path.find_last_of("\\/");
        if (pos != std::string::npos) path = path.substr(0, pos);
    }
    path += "/";
    path += relPath;
    return path;
}

// Resolve a path relative to %USERPROFILE%\Buzz\Gear\.
inline std::string GetGearPath(const char* relativePath) {
    char profileDir[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("USERPROFILE", profileDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "";
    return std::string(profileDir) + "\\Buzz\\Gear\\" + relativePath;
}
