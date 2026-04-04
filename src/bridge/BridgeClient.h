#pragma once

#include <windows.h>
#include "BridgeIPC.h"
#include <string>
#include <vector>

namespace BuzzVst {

// Client-side bridge used by the 64-bit VST3 processor to communicate
// with the 32-bit BuzzBridgeHost32.exe process.
class BridgeClient {
public:
	BridgeClient();
	~BridgeClient();

	// Launch the 32-bit bridge host process and connect.
	// hostExePath: full path to BuzzBridgeHost32.exe
	bool Start(const std::string& hostExePath);

	// Shut down the bridge host process.
	void Stop();

	bool IsRunning() const { return processRunning; }

	// Commands - mirror the Buzz machine loader API
	bool Ping();
	bool LoadDll(const std::string& dllPath);
	bool InitMachine();
	bool Unload();
	bool SetMasterInfo(int bpm, int sampleRate, int ticksPerBeat);
	bool Tick(const std::vector<BridgeTickParam>& params);
	bool Work(int numSamples, int workMode);
	bool StopMachine();
	bool SendMidiNote(int channel, int note, int velocity);
	bool SendMidiCC(int controller, int channel, int value);
	bool SetNumTracks(int numTracks);
	bool GetMachineInfo(BridgeMachineInfo& info, std::vector<BridgeParamInfo>& params);

	// Access shared audio memory
	BridgeSharedAudio* GetAudio();

private:
	bool SendAndWaitOk(BridgeCommand cmd, const void* payload = nullptr, uint32_t payloadSize = 0);

	std::string sessionId;
	BridgePipe pipe;
	BridgeSharedMem sharedMem;
	HANDLE hProcess = nullptr;
	bool processRunning = false;
};

} // namespace BuzzVst
