// Standalone debug test for FSM Infector
// Tests the machine at the lowest level to find why it produces no audio

#include "../src/buzz/BuzzMachineLoader.h"
#include "../src/buzz/MachineInterface.h"
#include "../src/common/SEHGuard.h"
#include "TestHelpers.h"
#include <cstdio>
#include <cstring>

using namespace BuzzVst;

int main() {
    printf("=== FSM Infector Debug Test ===\n\n");

    std::string path = GetGearPath("generators\\FSM Infector.dll");

    BuzzMachineLoader loader;
    if (!loader.Load(path.c_str())) {
        printf("FAILED to load DLL\n");
        return 1;
    }
    printf("DLL loaded OK\n");

    auto* info = loader.GetInfo();
    printf("Machine: %s\n", info->Name);
    printf("Type: %d, Flags: 0x%x\n", info->Type, info->Flags);
    printf("Global params: %d, Track params: %d\n", info->numGlobalParameters, info->numTrackParameters);
    printf("minTracks: %d, maxTracks: %d\n", info->minTracks, info->maxTracks);
    printf("numAttributes: %d\n", info->numAttributes);

    // Print all parameters
    printf("\nGlobal Parameters:\n");
    for (int i = 0; i < info->numGlobalParameters; i++) {
        auto* p = info->Parameters[i];
        printf("  [%d] %s: type=%d min=%d max=%d noVal=%d def=%d flags=0x%x\n",
            i, p->Name, p->Type, p->MinValue, p->MaxValue, p->NoValue, p->DefValue, p->Flags);
    }
    printf("\nTrack Parameters:\n");
    for (int i = 0; i < info->numTrackParameters; i++) {
        auto* p = info->Parameters[info->numGlobalParameters + i];
        printf("  [%d] %s: type=%d min=%d max=%d noVal=%d def=%d flags=0x%x\n",
            i, p->Name, p->Type, p->MinValue, p->MaxValue, p->NoValue, p->DefValue, p->Flags);
    }

    if (info->numAttributes > 0) {
        printf("\nAttributes:\n");
        for (int i = 0; i < info->numAttributes; i++) {
            auto* a = info->Attributes[i];
            printf("  [%d] %s: min=%d max=%d def=%d\n",
                i, a->Name, a->MinValue, a->MaxValue, a->DefValue);
        }
    }

    loader.UpdateMasterInfo(125.0, 44100.0);

    if (!loader.InitMachine()) {
        printf("\nFAILED to init machine\n");
        return 1;
    }
    printf("\nMachine initialized OK\n");

    auto* machine = loader.GetMachine();
    printf("GlobalVals: %p\n", machine->GlobalVals);
    printf("TrackVals: %p\n", machine->TrackVals);
    printf("AttrVals: %p\n", machine->AttrVals);

    auto* layout = loader.GetParamLayout();
    auto& gSlots = layout->GetGlobalSlots();
    auto& tSlots = layout->GetTrackSlots();

    // Write defaults and tick
    layout->WriteAllDefaults(machine->GlobalVals);
    if (machine->TrackVals) {
        for (int i = 0; i < (int)tSlots.size(); i++) {
            if (tSlots[i].param->Flags & MPF_STATE)
                layout->WriteTrackParam(machine->TrackVals, 0, i, tSlots[i].param->DefValue);
            else
                layout->WriteTrackParam(machine->TrackVals, 0, i, tSlots[i].param->NoValue);
        }
    }
    printf("\nDefaults written, calling Tick...\n");
    SEH_Call([&]() { machine->Tick(); });

    // Write note
    layout->WriteAllNoValues(machine->GlobalVals);
    if (machine->TrackVals) {
        layout->WriteTrackAllNoValues(machine->TrackVals, 1);
        for (int i = 0; i < (int)tSlots.size(); i++) {
            if (tSlots[i].param->Type == pt_note) {
                layout->WriteTrackParam(machine->TrackVals, 0, i, 0x51); // C-5
                printf("Note 0x51 written to track param %d\n", i);
                if (i + 1 < (int)tSlots.size()) {
                    layout->WriteTrackParam(machine->TrackVals, 0, i+1,
                        tSlots[i+1].param->MaxValue); // max volume
                    printf("Volume %d written to track param %d\n", tSlots[i+1].param->MaxValue, i+1);
                }
                break;
            }
        }
    }

    // Dump TrackVals
    int structSize = layout->GetTrackStructSize();
    unsigned char* tv = (unsigned char*)machine->TrackVals;
    printf("TrackVals (%d bytes): ", structSize);
    for (int b = 0; b < structSize; b++) printf("%02X ", tv[b]);
    printf("\n");

    printf("Calling Tick with note...\n");
    SEH_Call([&]() { machine->Tick(); });

    // Try Work
    printf("\nCalling Work...\n");
    float maxSample = 0;
    int samplesPerTick = loader.GetMasterInfo()->SamplesPerTick;

    for (int block = 0; block < 100; block++) {
        float buffer[256];
        memset(buffer, 0, sizeof(buffer));

        bool hasOutput = false;
        bool ok = SEH_Call([&]() {
            hasOutput = machine->Work(buffer, 256, WM_WRITE);
        });

        for (int i = 0; i < 256; i++) {
            float a = buffer[i] < 0 ? -buffer[i] : buffer[i];
            if (a > maxSample) maxSample = a;
        }

        if (block < 5 || maxSample > 0) {
            printf("  Block %d: ok=%d hasOutput=%d buf[0]=%f max=%f\n",
                block, (int)ok, (int)hasOutput, buffer[0], maxSample);
        }

        loader.GetMasterInfo()->PosInTick += 256;

        if (maxSample > 0) {
            printf("\n*** GOT AUDIO at block %d! ***\n", block);
            break;
        }
    }

    if (maxSample == 0) {
        printf("\n*** NO AUDIO after 100 blocks ***\n");
    }

    return 0;
}
