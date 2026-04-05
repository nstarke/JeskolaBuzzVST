// Standalone test: load FSM Kick XP and check Work output with proper defaults + note

#include <windows.h>
#include <cfloat>
#include <cstdio>
#include <string>
#include "../src/buzz/BuzzMachineLoader.h"
#include "../src/common/SEHGuard.h"
#include "TestHelpers.h"

using namespace BuzzVst;

int main() {
    printf("FPU control word: 0x%08X\n", _controlfp(0, 0));

    std::string path1 = GetGearPath("generators\\FSM Kick XP.dll");
    std::string path2 = GetGearPath("generators\\FSM Infector.dll");
    const char* paths[] = { path1.c_str(), path2.c_str(), nullptr };

    for (int p = 0; paths[p]; p++) {
        printf("\n=== Testing: %s ===\n", paths[p]);

        BuzzMachineLoader loader;
        if (!loader.Load(paths[p])) {
            printf("  Load FAILED\n");
            continue;
        }

        loader.UpdateMasterInfo(125.0, 44100.0);
        if (!loader.InitMachine()) {
            printf("  InitMachine FAILED\n");
            continue;
        }

        auto* machine = loader.GetMachine();
        auto* layout = loader.GetParamLayout();
        auto& tSlots = layout->GetTrackSlots();

        // Write proper defaults (like the real Buzz host does)
        layout->WriteAllDefaults(machine->GlobalVals);
        if (machine->TrackVals && !tSlots.empty()) {
            for (int i = 0; i < (int)tSlots.size(); i++) {
                layout->WriteTrackParam(machine->TrackVals, 0, i, tSlots[i].param->DefValue);
            }
        }

        // First tick with defaults
        SEH_Call([&]() { machine->Tick(); });

        // Second tick with note trigger
        for (int i = 0; i < (int)tSlots.size(); i++) {
            if (tSlots[i].param->Type == pt_note) {
                layout->WriteTrackParam(machine->TrackVals, 0, i, 0x51); // C-5
                if (i + 1 < (int)tSlots.size())
                    layout->WriteTrackParam(machine->TrackVals, 0, i + 1, tSlots[i+1].param->MaxValue);
                break;
            }
        }
        SEH_Call([&]() { machine->Tick(); });

        // Work for several blocks
        float maxSample = 0;
        for (int block = 0; block < 50; block++) {
            float buffer[256] = {};
            bool hasOutput = false;
            SEH_Call([&]() { hasOutput = machine->Work(buffer, 256, WM_WRITE); });
            for (int i = 0; i < 256; i++) {
                float a = buffer[i] < 0 ? -buffer[i] : buffer[i];
                if (a > maxSample) maxSample = a;
            }
            if (maxSample > 0) break;
        }

        printf("  max=%f\n", maxSample);
        printf(maxSample > 0 ? "  *** AUDIO OK ***\n" : "  *** NO AUDIO ***\n");
    }

    return 0;
}
