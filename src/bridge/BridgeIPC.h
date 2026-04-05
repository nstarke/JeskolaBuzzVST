#pragma once

// IPC protocol between the 64-bit VST3 plugin and the 32-bit bridge host.
// Uses a named pipe for commands and shared memory for audio buffers.
//
// Architecture:
//   64-bit VST3 (client) <--named pipe--> 32-bit BuzzBridgeHost32.exe (server)
//                         <--shared mem--> (audio buffers)

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>

namespace BuzzVst {

// Maximum audio buffer size in samples (stereo, both directions)
static const int kBridgeMaxSamples = 4096;
static const int kBridgeMaxChannels = 2;
static const int kBridgeMaxParams = 256;

// Shared memory layout for audio transfer
#pragma pack(push, 1)
struct BridgeSharedAudio {
	// Input audio (host -> bridge, for effects)
	float inputLeft[kBridgeMaxSamples];
	float inputRight[kBridgeMaxSamples];

	// Output audio (bridge -> host)
	float outputLeft[kBridgeMaxSamples];
	float outputRight[kBridgeMaxSamples];

	// Number of samples in current block
	int32_t numSamples;

	// Work mode (WM_NOIO, WM_READ, WM_WRITE, WM_READWRITE)
	int32_t workMode;

	// Whether Work() returned true (has audio output)
	int32_t hasOutput;

	// Fast-path Work signaling: client writes numSamples+workMode, signals workReady.
	// Host processes, writes hasOutput, signals workDone. No pipe needed.
	volatile int32_t fastWorkReady;   // 1 = client has written work params
	volatile int32_t fastWorkDone;    // 1 = host has finished processing
};
#pragma pack(pop)

// Command IDs sent over the named pipe
enum BridgeCommand : uint32_t {
	kCmdPing = 1,           // Ping - check if host is alive. Response: kRespOk
	kCmdLoadDll,            // Load a Buzz machine DLL. Payload: null-terminated path string
	kCmdInitMachine,        // Call CreateMachine + Init. No payload.
	kCmdUnload,             // Unload current machine.
	kCmdSetMasterInfo,      // Update master info. Payload: BridgeMasterInfo
	kCmdTick,               // Call Tick(). Payload: BridgeTickData (param values)
	kCmdWork,               // Call Work(). Audio via shared memory. Payload: numSamples + workMode
	kCmdStop,               // Call machine->Stop()
	kCmdMidiNote,           // Send MIDI note. Payload: BridgeMidiNote
	kCmdMidiCC,             // Send MIDI CC. Payload: BridgeMidiCC
	kCmdGetInfo,            // Get machine info. Response: BridgeMachineInfo
	kCmdSetParam,           // Set a parameter value. Payload: BridgeParamValue
	kCmdLoadWave,           // Load a WAV into wave table. Payload: BridgeLoadWave
	kCmdClearWaves,         // Clear all wave table slots. No payload.
	kCmdSetNumTracks,       // Set number of active tracks. Payload: int32_t
	kCmdDescribeValue,      // Get string description for a param value. Payload: BridgeDescribeValue
	kCmdShutdown,           // Graceful shutdown of the bridge process
};

// Response codes
enum BridgeResponse : uint32_t {
	kRespOk = 0,
	kRespError,
	kRespMachineInfo,       // Followed by BridgeMachineInfo
	kRespDescribeValue,     // Followed by null-terminated string
};

// Wire format for a command header
#pragma pack(push, 1)
struct BridgeCmdHeader {
	BridgeCommand cmd;
	uint32_t payloadSize;
};

struct BridgeRespHeader {
	BridgeResponse resp;
	uint32_t payloadSize;
};

struct BridgeMasterInfo {
	int32_t beatsPerMin;
	int32_t ticksPerBeat;
	int32_t samplesPerSec;
};

struct BridgeMidiNote {
	int32_t channel;
	int32_t note;
	int32_t velocity;
};

struct BridgeMidiCC {
	int32_t controller;
	int32_t channel;
	int32_t value;
};

struct BridgeParamValue {
	int32_t paramId;     // global slot index (0-based) or track slot (offset by 1000)
	int32_t value;
};

// Parameter info sent from bridge -> host
struct BridgeParamInfo {
	int32_t type;        // CMPType
	int32_t minValue;
	int32_t maxValue;
	int32_t noValue;
	int32_t defValue;
	int32_t flags;
	char name[64];
	char description[128];
};

struct BridgeMachineInfo {
	int32_t type;           // MT_GENERATOR or MT_EFFECT
	int32_t flags;          // MIF_* flags
	int32_t numGlobalParams;
	int32_t numTrackParams;
	int32_t minTracks;
	int32_t maxTracks;
	char name[128];
	char shortName[64];
	char author[64];
	// Followed by (numGlobalParams + numTrackParams) * BridgeParamInfo
};

struct BridgeWorkCmd {
	int32_t numSamples;
	int32_t workMode;
};

struct BridgeLoadWave {
	int32_t slotIndex;      // 1-based wave slot, or 0 for auto
	int32_t pathLen;        // Length of file path string (follows this struct)
};

struct BridgeDescribeValue {
	int32_t param;          // Parameter index (flat, across global+track)
	int32_t value;          // Buzz integer value
};

// Tick data: array of changed param values, terminated by paramId == -1
struct BridgeTickParam {
	int32_t paramId;    // -1 = end sentinel
	int32_t value;
};
#pragma pack(pop)

// Helper class for pipe I/O
class BridgePipe {
public:
	BridgePipe() : hPipe(INVALID_HANDLE_VALUE) {}
	~BridgePipe() { Close(); }

	bool IsValid() const { return hPipe != INVALID_HANDLE_VALUE; }
	HANDLE GetHandle() const { return hPipe; }
	void SetHandle(HANDLE h) { hPipe = h; }

	void Close() {
		if (hPipe != INVALID_HANDLE_VALUE) {
			CloseHandle(hPipe);
			hPipe = INVALID_HANDLE_VALUE;
		}
	}

	bool WriteAll(const void* data, uint32_t size) {
		const char* ptr = (const char*)data;
		uint32_t remaining = size;
		while (remaining > 0) {
			DWORD written = 0;
			if (!WriteFile(hPipe, ptr, remaining, &written, nullptr))
				return false;
			ptr += written;
			remaining -= written;
		}
		return true;
	}

	bool ReadAll(void* data, uint32_t size) {
		char* ptr = (char*)data;
		uint32_t remaining = size;
		while (remaining > 0) {
			DWORD read = 0;
			if (!ReadFile(hPipe, ptr, remaining, &read, nullptr) || read == 0)
				return false;
			ptr += read;
			remaining -= read;
		}
		return true;
	}

	bool SendCommand(BridgeCommand cmd, const void* payload = nullptr, uint32_t payloadSize = 0) {
		BridgeCmdHeader hdr = { cmd, payloadSize };
		if (!WriteAll(&hdr, sizeof(hdr))) return false;
		if (payloadSize > 0 && payload) {
			if (!WriteAll(payload, payloadSize)) return false;
		}
		return true;
	}

	bool ReadResponse(BridgeRespHeader& resp) {
		return ReadAll(&resp, sizeof(resp));
	}

	bool SendResponse(BridgeResponse code, const void* payload = nullptr, uint32_t payloadSize = 0) {
		BridgeRespHeader hdr = { code, payloadSize };
		if (!WriteAll(&hdr, sizeof(hdr))) return false;
		if (payloadSize > 0 && payload) {
			if (!WriteAll(payload, payloadSize)) return false;
		}
		return true;
	}

	bool ReadCommand(BridgeCmdHeader& cmd) {
		return ReadAll(&cmd, sizeof(cmd));
	}

private:
	HANDLE hPipe;
};

// Shared memory wrapper
class BridgeSharedMem {
public:
	BridgeSharedMem() : hMapping(nullptr), pData(nullptr) {}
	~BridgeSharedMem() { Close(); }

	bool Create(const std::string& name, uint32_t size) {
		hMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
			PAGE_READWRITE, 0, size, name.c_str());
		if (!hMapping) return false;
		pData = MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
		if (!pData) { CloseHandle(hMapping); hMapping = nullptr; return false; }
		memset(pData, 0, size);
		return true;
	}

	bool Open(const std::string& name, uint32_t size) {
		hMapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
		if (!hMapping) return false;
		pData = MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
		if (!pData) { CloseHandle(hMapping); hMapping = nullptr; return false; }
		return true;
	}

	void Close() {
		if (pData) { UnmapViewOfFile(pData); pData = nullptr; }
		if (hMapping) { CloseHandle(hMapping); hMapping = nullptr; }
	}

	void* GetData() { return pData; }
	BridgeSharedAudio* GetAudio() { return (BridgeSharedAudio*)pData; }

private:
	HANDLE hMapping;
	void* pData;
};

// Generate unique names for IPC objects based on a session ID
inline std::string BridgePipeName(const std::string& sessionId) {
	return "\\\\.\\pipe\\BuzzBridge_" + sessionId;
}

inline std::string BridgeSharedMemName(const std::string& sessionId) {
	return "Local\\BuzzBridge_Audio_" + sessionId;
}

} // namespace BuzzVst
