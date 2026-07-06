// ParamDump: dump every Buzz machine's parameter/attribute metadata as JSON.
//
// Used by external tooling (e.g. the Renoise-XRNS jeskola-buzz-song skill) to
// build a machine->parameter map for programmatically authoring BuzzBridge
// VST3 state chunks. Runs each machine in a child process with a timeout,
// like BuzzMachineTests, so crashers can't kill the sweep.
//
// Build target: ParamDump (32-bit only)
// Usage: ParamDump.exe <gear_dir> <out.json>
//        ParamDump.exe --dump-one <dll_path> <out.json>   (child mode)
//
// Output (parent mode): one JSON object; "machines" is a list of per-machine
// dumps in gear-scan order. Per machine:
//   { "name", "shortName", "author", "dllPath", "category",
//     "type": "generator"|"effect", "flags", "minTracks", "maxTracks",
//     "globalParams": [ {"name","description","type","min","max","noValue",
//                        "default","flags","valueDescriptions":{...}} ],
//     "trackParams": [ ... same shape ... ],
//     "attributes": [ {"name","min","max","default"} ],
//     "initOk": bool }   // false: params dumped but Init/DescribeValue skipped

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "../src/buzz/BuzzMachineLoader.h"
#include "../src/buzz/BuzzParamLayout.h"
#include "../src/vst3/GearScanner.h"
#include "../src/common/SEHGuard.h"
#include "../src/common/PatchMessageBoxes.h"

using namespace BuzzVst;

static const int TIMEOUT_MS = 15000;

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static std::string jsonEscape(const char* s) {
    std::string out;
    if (!s) return out;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c > 0x7E) {
                    // Machine strings are ANSI; escape anything non-ASCII so
                    // the output is valid UTF-8 JSON regardless of codepage.
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

static const char* paramTypeName(int t) {
    switch (t) {
        case pt_note:   return "note";
        case pt_switch: return "switch";
        case pt_byte:   return "byte";
        case pt_word:   return "word";
        default:        return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Child mode: dump a single machine's metadata to a JSON file
// ---------------------------------------------------------------------------

static void dumpParamList(FILE* f, const std::vector<ParamSlot>& slots,
                          CMachineInterface* machine, int paramIndexBase) {
    for (size_t i = 0; i < slots.size(); i++) {
        const CMachineParameter* p = slots[i].param;
        fprintf(f, "%s      {\"name\": \"%s\", \"description\": \"%s\", "
                   "\"type\": \"%s\", \"min\": %d, \"max\": %d, "
                   "\"noValue\": %d, \"default\": %d, \"flags\": %d",
                i ? ",\n" : "",
                jsonEscape(p->Name).c_str(),
                jsonEscape(p->Description).c_str(),
                paramTypeName(p->Type),
                p->MinValue, p->MaxValue, p->NoValue, p->DefValue, p->Flags);

        // Value descriptions (enum labels), same range gate as the plugin.
        int range = p->MaxValue - p->MinValue;
        if (machine && range > 0 && range <= 64) {
            std::string descs;
            bool hasAny = false;
            for (int v = p->MinValue; v <= p->MaxValue; v++) {
                const char* d = nullptr;
                bool ok = SEH_Call([&]() {
                    d = machine->DescribeValue(paramIndexBase + (int)i, v);
                });
                if (ok && d && *d) {
                    if (hasAny) descs += ", ";
                    char key[16];
                    snprintf(key, sizeof(key), "\"%d\": ", v);
                    descs += key;
                    descs += "\"" + jsonEscape(d) + "\"";
                    hasAny = true;
                }
            }
            if (hasAny)
                fprintf(f, ", \"valueDescriptions\": {%s}", descs.c_str());
        }
        fprintf(f, "}");
    }
    if (!slots.empty()) fprintf(f, "\n");
}

static int dumpOneMachine(const char* dllPath, const char* outPath) {
    BuzzMachineLoader loader;
    if (!loader.Load(dllPath)) return 3;

    const CMachineInfo* info = loader.GetInfo();
    if (!info) return 3;

    loader.UpdateMasterInfo(125.0, 44100.0);

    // Init the machine so DescribeValue works; many machines only fill
    // internal tables in Init. If Init crashes or fails we still dump the
    // static metadata, just without value descriptions.
    bool initOk = false;
    SEH_Call([&]() { initOk = loader.InitMachine(); });
    CMachineInterface* machine = initOk ? loader.GetMachine() : nullptr;

    FILE* f = fopen(outPath, "w");
    if (!f) return 2;

    auto* layout = loader.GetParamLayout();
    const auto& gSlots = layout->GetGlobalSlots();
    const auto& tSlots = layout->GetTrackSlots();

    fprintf(f, "{\n");
    fprintf(f, "  \"name\": \"%s\",\n", jsonEscape(info->Name).c_str());
    fprintf(f, "  \"shortName\": \"%s\",\n", jsonEscape(info->ShortName).c_str());
    fprintf(f, "  \"author\": \"%s\",\n", jsonEscape(info->Author).c_str());
    fprintf(f, "  \"type\": \"%s\",\n",
            info->Type == MT_GENERATOR ? "generator" :
            info->Type == MT_EFFECT ? "effect" : "other");
    fprintf(f, "  \"flags\": %d,\n", info->Flags);
    fprintf(f, "  \"minTracks\": %d,\n", info->minTracks);
    fprintf(f, "  \"maxTracks\": %d,\n", info->maxTracks);
    fprintf(f, "  \"initOk\": %s,\n", initOk ? "true" : "false");

    fprintf(f, "  \"globalParams\": [\n");
    dumpParamList(f, gSlots, machine, 0);
    fprintf(f, "  ],\n");

    fprintf(f, "  \"trackParams\": [\n");
    dumpParamList(f, tSlots, machine, (int)gSlots.size());
    fprintf(f, "  ],\n");

    fprintf(f, "  \"attributes\": [\n");
    for (int i = 0; i < info->numAttributes; i++) {
        const CMachineAttribute* a = info->Attributes ? info->Attributes[i] : nullptr;
        if (!a) continue;
        fprintf(f, "%s    {\"name\": \"%s\", \"min\": %d, \"max\": %d, \"default\": %d}",
                i ? ",\n" : "", jsonEscape(a->Name).c_str(),
                a->MinValue, a->MaxValue, a->DefValue);
    }
    if (info->numAttributes > 0) fprintf(f, "\n");
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
    return 0;
}

// ---------------------------------------------------------------------------
// Parent mode: sweep the gear dir, one child per machine, aggregate JSON
// ---------------------------------------------------------------------------

static bool shouldSkip(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    size_t lastSlash = lower.find_last_of("\\/");
    std::string filename = (lastSlash != std::string::npos)
        ? lower.substr(lastSlash + 1) : lower;
    return filename.find("vst") != std::string::npos ||
           filename.find("peer") != std::string::npos ||
           filename.find("input") != std::string::npos ||
           filename.find("wavein") != std::string::npos;
}

static int runChildDump(const char* myExe, const std::string& dllPath,
                        const std::string& outPath) {
    std::string cmdLine = "\"";
    cmdLine += myExe;
    cmdLine += "\" --dump-one \"";
    cmdLine += dllPath;
    cmdLine += "\" \"";
    cmdLine += outPath;
    cmdLine += "\"";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, (char*)cmdLine.c_str(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return 3;

    int rc;
    DWORD waitResult = WaitForSingleObject(pi.hProcess, TIMEOUT_MS);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 99);
        WaitForSingleObject(pi.hProcess, 1000);
        rc = 5;
    } else {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        rc = (int)code;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return rc;
}

int main(int argc, char* argv[]) {
    PatchMessageBoxes();

    if (argc >= 4 && strcmp(argv[1], "--dump-one") == 0) {
        int rc = dumpOneMachine(argv[2], argv[3]);
        fflush(stdout);
        // Skip all CRT/DLL cleanup: some machines' static destructors crash.
        TerminateProcess(GetCurrentProcess(), (UINT)rc);
        return rc;
    }

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <gear_dir> <out.json>\n", argv[0]);
        fprintf(stderr, "       %s --dump-one <dll_path> <out.json>\n", argv[0]);
        return 1;
    }
    const char* gearDir = argv[1];
    const char* outJson = argv[2];

    char myExe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, myExe, MAX_PATH);

    GearScanner scanner;
    if (!scanner.Scan(gearDir)) {
        fprintf(stderr, "Failed to scan gear directory: %s\n", gearDir);
        return 1;
    }

    char tmpDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tmpDir);

    FILE* out = fopen(outJson, "w");
    if (!out) {
        fprintf(stderr, "Cannot write %s\n", outJson);
        return 1;
    }
    fprintf(out, "{\n\"gearDir\": \"%s\",\n\"machines\": [\n", jsonEscape(gearDir).c_str());

    auto& entries = scanner.GetEntries();
    printf("Found %d machines total\n", (int)entries.size());
    int dumped = 0, failed = 0, emitted = 0;

    for (auto& e : entries) {
        if (e.machineType != MT_GENERATOR && e.machineType != MT_EFFECT) continue;
        if (shouldSkip(e.dllPath)) continue;

        printf("  %-40s ", e.displayName.c_str());
        fflush(stdout);

        char tmpFile[MAX_PATH];
        snprintf(tmpFile, sizeof(tmpFile), "%sparamdump_%d.json", tmpDir, emitted + failed);
        DeleteFileA(tmpFile);

        int rc = runChildDump(myExe, e.dllPath, tmpFile);

        // Use the child's file if it appeared, even if the child crashed
        // afterwards (metadata is written before DescribeValue probing).
        FILE* tf = fopen(tmpFile, "rb");
        if (!tf) {
            printf("FAIL (rc=%d)\n", rc);
            failed++;
            continue;
        }
        fseek(tf, 0, SEEK_END);
        long sz = ftell(tf);
        fseek(tf, 0, SEEK_SET);
        std::string body((size_t)sz, 0);
        fread(&body[0], 1, (size_t)sz, tf);
        fclose(tf);
        DeleteFileA(tmpFile);

        // A crashed child can leave a truncated file; require the closing
        // brace so the aggregate stays valid JSON.
        size_t lastBrace = body.find_last_of('}');
        if (lastBrace == std::string::npos) {
            printf("FAIL (truncated, rc=%d)\n", rc);
            failed++;
            continue;
        }
        body.resize(lastBrace + 1);

        // Inject dllPath/category into the child's object.
        std::string extra = "{\n  \"dllPath\": \"" + jsonEscape(e.dllPath.c_str()) +
            "\",\n  \"category\": \"" + jsonEscape(e.category.c_str()) + "\",";
        body.replace(0, 1, extra);

        fprintf(out, "%s%s", emitted ? ",\n" : "", body.c_str());
        printf(rc == 0 ? "OK\n" : "OK (partial, rc=%d)\n", rc);
        dumped++;
        emitted++;
    }

    fprintf(out, "\n]\n}\n");
    fclose(out);
    printf("\nDumped %d machines (%d failed) -> %s\n", dumped, failed, outJson);
    return 0;
}
