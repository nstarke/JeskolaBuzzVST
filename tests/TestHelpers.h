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

// Resolve a path relative to %USERPROFILE%\Buzz\Gear\.
inline std::string GetGearPath(const char* relativePath) {
    char profileDir[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("USERPROFILE", profileDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "";
    return std::string(profileDir) + "\\Buzz\\Gear\\" + relativePath;
}

// Return the Buzz Gear directory (no trailing slash).
inline std::string GetGearDir() {
    char profileDir[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("USERPROFILE", profileDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "";
    return std::string(profileDir) + "\\Buzz\\Gear";
}
