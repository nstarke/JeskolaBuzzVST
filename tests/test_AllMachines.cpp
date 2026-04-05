// Extended test suite: loads every Buzz machine DLL (excluding VST/Peer)
// and verifies it can initialize and produce audio.
//
// Each machine is tested in a child process with a timeout to handle hangs.
//
// Build target: BuzzMachineTests (32-bit only)
// Usage: BuzzMachineTests.exe [gear_dir]
//        BuzzMachineTests.exe --test-one <gen|fx> <dll_path>   (child mode)
//   Default gear_dir: %USERPROFILE%\Buzz\Gear

#include <windows.h>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include "../src/buzz/BuzzMachineLoader.h"
#include "../src/buzz/BuzzParamLayout.h"
#include "../src/vst3/GearScanner.h"
#include "../src/common/SEHGuard.h"
#include "../src/common/PatchMessageBoxes.h"

using namespace BuzzVst;

static const int TIMEOUT_MS = 10000; // 10 seconds per machine

// ============================================================================
// Child process mode: test a single machine and exit with a status code
// Exit codes: 0 = audio OK, 1 = no audio, 2 = init fail, 3 = load fail, 4 = crash
// Writes max sample to stdout as a float
// ============================================================================

static int testOneGenerator(const char* dllPath) {
    BuzzMachineLoader loader;
    if (!loader.Load(dllPath)) return 3;

    loader.UpdateMasterInfo(125.0, 44100.0);

    bool initOk = false;
    bool ok = SEH_Call([&]() { initOk = loader.InitMachine(); });
    if (!ok) return 4;
    if (!initOk) return 2;

    auto* machine = loader.GetMachine();
    auto* layout = loader.GetParamLayout();
    auto* info = loader.GetInfo();
    if (!machine || !layout || !info) return 2;

    auto& gSlots = layout->GetGlobalSlots();
    auto& tSlots = layout->GetTrackSlots();

    // First tick: defaults
    if (machine->GlobalVals)
        layout->WriteAllDefaults(machine->GlobalVals);
    if (machine->TrackVals && !tSlots.empty()) {
        for (int i = 0; i < (int)tSlots.size(); i++)
            layout->WriteTrackParam(machine->TrackVals, 0, i, tSlots[i].param->DefValue);
    }
    SEH_Call([&]() { machine->Tick(); });

    // Second tick: trigger note
    if (machine->GlobalVals)
        layout->WriteAllNoValues(machine->GlobalVals);
    if (machine->TrackVals && !tSlots.empty())
        layout->WriteTrackAllNoValues(machine->TrackVals, 1);

    bool noteTriggered = false;
    for (int i = 0; i < (int)tSlots.size(); i++) {
        if (tSlots[i].param->Type == pt_note) {
            layout->WriteTrackParam(machine->TrackVals, 0, i, 0x51);
            if (i + 1 < (int)tSlots.size() && tSlots[i+1].param->Type == pt_byte)
                layout->WriteTrackParam(machine->TrackVals, 0, i + 1, tSlots[i+1].param->MaxValue);
            noteTriggered = true;
            break;
        }
    }
    if (!noteTriggered) {
        for (int i = 0; i < (int)gSlots.size(); i++) {
            if (gSlots[i].param->Type == pt_note && machine->GlobalVals) {
                layout->WriteGlobalParam(machine->GlobalVals, i, 0x51);
                if (i + 1 < (int)gSlots.size() && gSlots[i+1].param->Type == pt_byte)
                    layout->WriteGlobalParam(machine->GlobalVals, i + 1, gSlots[i+1].param->MaxValue);
                noteTriggered = true;
                break;
            }
        }
    }
    // Fallback: trigger pt_switch params (machines like ErsBlipp use Trig switch)
    if (!noteTriggered) {
        for (int i = 0; i < (int)tSlots.size(); i++) {
            if (tSlots[i].param->Type == pt_switch &&
                tSlots[i].param->MinValue == 1 && tSlots[i].param->MaxValue == 1) {
                layout->WriteTrackParam(machine->TrackVals, 0, i, 1);
                noteTriggered = true;
                break;
            }
        }
    }
    // Fallback: non-state global byte/word as velocity trigger (ld clap pattern)
    if (!noteTriggered && machine->GlobalVals) {
        for (int i = 0; i < (int)gSlots.size(); i++) {
            if ((gSlots[i].param->Type == pt_byte || gSlots[i].param->Type == pt_word) &&
                !(gSlots[i].param->Flags & MPF_STATE)) {
                layout->WriteGlobalParam(machine->GlobalVals, i, gSlots[i].param->MaxValue);
                break;
            }
        }
    }
    SEH_Call([&]() { machine->Tick(); });

    bool monoToStereo = (info->Flags & MIF_MONO_TO_STEREO) != 0;
    float maxSample = 0;

    for (int block = 0; block < 100; block++) {
        float bufL[MAX_BUFFER_LENGTH] = {};
        float bufR[MAX_BUFFER_LENGTH] = {};
        bool hasOutput = false;

        if (monoToStereo) {
            SEH_Call([&]() { hasOutput = machine->WorkMonoToStereo(bufL, bufR, MAX_BUFFER_LENGTH, WM_WRITE); });
        } else {
            SEH_Call([&]() { hasOutput = machine->Work(bufL, MAX_BUFFER_LENGTH, WM_WRITE); });
        }

        if (hasOutput) {
            for (int i = 0; i < MAX_BUFFER_LENGTH; i++) {
                float a = fabsf(bufL[i]);
                if (a > maxSample) maxSample = a;
                if (monoToStereo) {
                    a = fabsf(bufR[i]);
                    if (a > maxSample) maxSample = a;
                }
            }
        }

        if (block % 10 == 9) {
            if (machine->GlobalVals) layout->WriteAllNoValues(machine->GlobalVals);
            if (machine->TrackVals && !tSlots.empty()) layout->WriteTrackAllNoValues(machine->TrackVals, 1);
            SEH_Call([&]() { machine->Tick(); });
        }

        if (maxSample > 0) break;
    }

    printf("%.1f", maxSample);
    return (maxSample > 0) ? 0 : 1;
}

static int testOneEffect(const char* dllPath) {
    BuzzMachineLoader loader;
    if (!loader.Load(dllPath)) return 3;

    loader.UpdateMasterInfo(125.0, 44100.0);

    bool initOk = false;
    bool ok = SEH_Call([&]() { initOk = loader.InitMachine(); });
    if (!ok) return 4;
    if (!initOk) return 2;

    auto* machine = loader.GetMachine();
    auto* layout = loader.GetParamLayout();
    auto* info = loader.GetInfo();
    if (!machine || !layout || !info) return 2;

    auto& tSlots = layout->GetTrackSlots();

    if (machine->GlobalVals) layout->WriteAllDefaults(machine->GlobalVals);
    if (machine->TrackVals && !tSlots.empty()) {
        for (int i = 0; i < (int)tSlots.size(); i++)
            layout->WriteTrackParam(machine->TrackVals, 0, i, tSlots[i].param->DefValue);
    }
    SEH_Call([&]() { machine->Tick(); });

    bool monoToStereo = (info->Flags & MIF_MONO_TO_STEREO) != 0;
    bool isMDK = loader.GetCallbacks()->isMDKMachine;
    float maxSample = 0;

    for (int block = 0; block < 20; block++) {
        bool hasOutput = false;

        if (isMDK) {
            // MDK machines expect stereo interleaved: [L0,R0,L1,R1,...]
            float interleaved[MAX_BUFFER_LENGTH * 2];
            for (int i = 0; i < MAX_BUFFER_LENGTH; i++) {
                float t = (float)(block * MAX_BUFFER_LENGTH + i) / 44100.0f;
                float s = 16384.0f * sinf(2.0f * 3.14159265f * 440.0f * t);
                interleaved[i * 2] = s;
                interleaved[i * 2 + 1] = s;
            }
            SEH_Call([&]() { hasOutput = machine->Work(interleaved, MAX_BUFFER_LENGTH, WM_READWRITE); });
            if (hasOutput) {
                for (int i = 0; i < MAX_BUFFER_LENGTH * 2; i++) {
                    float a = fabsf(interleaved[i]);
                    if (a > maxSample) maxSample = a;
                }
            }
        } else {
            float bufL[MAX_BUFFER_LENGTH];
            float bufR[MAX_BUFFER_LENGTH];
            for (int i = 0; i < MAX_BUFFER_LENGTH; i++) {
                float t = (float)(block * MAX_BUFFER_LENGTH + i) / 44100.0f;
                bufL[i] = 16384.0f * sinf(2.0f * 3.14159265f * 440.0f * t);
                bufR[i] = bufL[i];
            }

            if (monoToStereo) {
                SEH_Call([&]() { hasOutput = machine->WorkMonoToStereo(bufL, bufR, MAX_BUFFER_LENGTH, WM_READWRITE); });
            } else {
                SEH_Call([&]() { hasOutput = machine->Work(bufL, MAX_BUFFER_LENGTH, WM_READWRITE); });
            }

            if (hasOutput) {
                for (int i = 0; i < MAX_BUFFER_LENGTH; i++) {
                    float a = fabsf(bufL[i]);
                    if (a > maxSample) maxSample = a;
                    if (monoToStereo) {
                        a = fabsf(bufR[i]);
                        if (a > maxSample) maxSample = a;
                    }
                }
            }
        }

        if (block % 5 == 4) {
            if (machine->GlobalVals) layout->WriteAllNoValues(machine->GlobalVals);
            if (machine->TrackVals && !tSlots.empty()) layout->WriteTrackAllNoValues(machine->TrackVals, 1);
            SEH_Call([&]() { machine->Tick(); });
        }

        if (maxSample > 0) break;
    }

    printf("%.1f", maxSample);
    return (maxSample > 0) ? 0 : 1;
}

// ============================================================================
// Parent process: spawn child for each machine with a timeout
// ============================================================================

static bool shouldSkip(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    size_t lastSlash = lower.find_last_of("\\/");
    std::string filename = (lastSlash != std::string::npos)
        ? lower.substr(lastSlash + 1) : lower;
    return filename.find("vst") != std::string::npos ||
           filename.find("peer") != std::string::npos;
}

struct MachineTestResult {
    std::string name;
    std::string path;
    int machineType;
    int exitCode;    // 0=audio, 1=no audio, 2=init fail, 3=load fail, 4=crash, 5=timeout
    std::string output;
};

static MachineTestResult runChildTest(const char* myExe, const GearEntry& entry) {
    MachineTestResult r = {};
    r.name = entry.displayName;
    r.path = entry.dllPath;
    r.machineType = entry.machineType;

    const char* typeArg = (entry.machineType == MT_GENERATOR) ? "gen" : "fx";

    // Build command line
    std::string cmdLine = "\"";
    cmdLine += myExe;
    cmdLine += "\" --test-one ";
    cmdLine += typeArg;
    cmdLine += " \"";
    cmdLine += entry.dllPath;
    cmdLine += "\"";

    // Create pipe for stdout
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hReadPipe, hWritePipe;
    CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, (char*)cmdLine.c_str(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        r.exitCode = 3;
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return r;
    }

    CloseHandle(hWritePipe);

    // Wait with timeout
    DWORD waitResult = WaitForSingleObject(pi.hProcess, TIMEOUT_MS);

    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 99);
        WaitForSingleObject(pi.hProcess, 1000);
        r.exitCode = 5; // timeout
    } else {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        r.exitCode = (int)code;
    }

    // Read child output
    char buf[256] = {};
    DWORD bytesRead = 0;
    ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr);
    buf[bytesRead] = 0;
    r.output = buf;

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return r;
}

int main(int argc, char* argv[]) {
    PatchMessageBoxes();

    // Child mode: test a single machine
    if (argc >= 4 && strcmp(argv[1], "--test-one") == 0) {
        const char* type = argv[2];
        const char* path = argv[3];
        if (strcmp(type, "gen") == 0)
            return testOneGenerator(path);
        else
            return testOneEffect(path);
    }

    // Parent mode: scan and test all machines
    std::string gearDir;
    if (argc > 1) {
        gearDir = argv[1];
    } else {
        char profileDir[MAX_PATH] = {};
        DWORD len = GetEnvironmentVariableA("USERPROFILE", profileDir, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
            gearDir = std::string(profileDir) + "\\Buzz\\Gear";
    }

    if (gearDir.empty()) {
        fprintf(stderr, "Usage: %s [gear_directory]\n", argv[0]);
        return 1;
    }

    // Get our own exe path for spawning children
    char myExe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, myExe, MAX_PATH);

    printf("Scanning: %s\n", gearDir.c_str());

    GearScanner scanner;
    if (!scanner.Scan(gearDir)) {
        fprintf(stderr, "Failed to scan gear directory: %s\n", gearDir.c_str());
        return 1;
    }

    auto& entries = scanner.GetEntries();
    printf("Found %d machines total\n", (int)entries.size());

    std::vector<const GearEntry*> generators, effects;
    int skippedCount = 0;
    for (auto& e : entries) {
        if (shouldSkip(e.dllPath)) { skippedCount++; continue; }
        if (e.machineType == MT_GENERATOR) generators.push_back(&e);
        else if (e.machineType == MT_EFFECT) effects.push_back(&e);
    }

    printf("Testing %d generators, %d effects (skipped %d VST/Peer)\n\n",
        (int)generators.size(), (int)effects.size(), skippedCount);

    std::vector<MachineTestResult> results;
    int passed = 0, noAudio = 0, loadFailed = 0, initFailed = 0, crashed = 0, timedOut = 0;

    auto runBatch = [&](const char* label, const std::vector<const GearEntry*>& batch) {
        printf("=== %s ===\n", label);
        for (auto* e : batch) {
            printf("  %-40s ", e->displayName.c_str());
            fflush(stdout);

            MachineTestResult r = runChildTest(myExe, *e);
            results.push_back(r);

            switch (r.exitCode) {
                case 0:
                    printf("OK (max=%s)\n", r.output.c_str());
                    passed++;
                    break;
                case 1:
                    printf("NO AUDIO\n");
                    noAudio++;
                    break;
                case 2:
                    printf("INIT FAIL\n");
                    initFailed++;
                    break;
                case 3:
                    printf("LOAD FAIL\n");
                    loadFailed++;
                    break;
                case 4:
                    printf("CRASH\n");
                    crashed++;
                    break;
                case 5:
                    printf("TIMEOUT (%ds)\n", TIMEOUT_MS / 1000);
                    timedOut++;
                    break;
                default:
                    printf("ERROR (code=%d)\n", r.exitCode);
                    crashed++;
                    break;
            }
        }
        printf("\n");
    };

    runBatch("GENERATORS", generators);
    runBatch("EFFECTS", effects);

    int total = (int)results.size();
    printf("========================================\n");
    printf("Results: %d/%d passed\n", passed, total);
    printf("  Audio OK:    %d\n", passed);
    printf("  No audio:    %d\n", noAudio);
    printf("  Load failed: %d\n", loadFailed);
    printf("  Init failed: %d\n", initFailed);
    printf("  Crashed:     %d\n", crashed);
    printf("  Timed out:   %d\n", timedOut);
    printf("========================================\n");

    if (passed < total) {
        printf("\nFailed machines:\n");
        for (auto& r : results) {
            if (r.exitCode == 0) continue;
            const char* reason =
                r.exitCode == 1 ? "NO AUDIO" :
                r.exitCode == 2 ? "INIT" :
                r.exitCode == 3 ? "LOAD" :
                r.exitCode == 4 ? "CRASH" :
                r.exitCode == 5 ? "TIMEOUT" : "ERROR";
            const char* type = (r.machineType == MT_GENERATOR) ? "GEN" : "FX";
            printf("  [%s] %-40s %s\n", type, r.name.c_str(), reason);
        }
    }

    return (passed == total) ? 0 : 1;
}
