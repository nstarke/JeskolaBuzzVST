#include <windows.h>
#include "BridgeClient.h"
#include <cstdio>
#include <cstring>

namespace BuzzVst {

BridgeClient::BridgeClient()
{
}

BridgeClient::~BridgeClient()
{
	Stop();
}

bool BridgeClient::Start(const std::string& hostExePath)
{
	Stop();

	// Generate a unique session ID from current time + process ID
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	char idBuf[64];
	snprintf(idBuf, sizeof(idBuf), "%u_%lld", GetCurrentProcessId(), counter.QuadPart);
	sessionId = idBuf;

	// Launch the 32-bit host process
	std::string cmdLine = "\"" + hostExePath + "\" " + sessionId;

	char dbg[512];
	snprintf(dbg, sizeof(dbg), "[BuzzBridge64] Launching: %s\n", cmdLine.c_str());
	OutputDebugStringA(dbg);

	STARTUPINFOA si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION pi = {};

	if (!CreateProcessA(
		nullptr,
		(char*)cmdLine.c_str(),
		nullptr, nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr, nullptr,
		&si, &pi))
	{
		snprintf(dbg, sizeof(dbg), "[BuzzBridge64] CreateProcess failed (error %lu)\n", GetLastError());
		OutputDebugStringA(dbg);
		return false;
	}

	snprintf(dbg, sizeof(dbg), "[BuzzBridge64] Process launched (PID %lu), waiting for pipe...\n", pi.dwProcessId);
	OutputDebugStringA(dbg);

	hProcess = pi.hProcess;
	CloseHandle(pi.hThread);
	processRunning = true;

	// Wait for the pipe to become available.
	// The host process needs time to start up and create the pipe.
	// WaitNamedPipe only works if the pipe already exists, so we poll
	// with CreateFile attempts + Sleep.
	std::string pipeName = BridgePipeName(sessionId);
	snprintf(dbg, sizeof(dbg), "[BuzzBridge64] Waiting for pipe: %s\n", pipeName.c_str());
	OutputDebugStringA(dbg);

	HANDLE hPipe = INVALID_HANDLE_VALUE;
	for (int attempt = 0; attempt < 100; attempt++) { // up to 10 seconds
		hPipe = CreateFileA(
			pipeName.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			0, nullptr,
			OPEN_EXISTING,
			0, nullptr
		);

		if (hPipe != INVALID_HANDLE_VALUE) {
			snprintf(dbg, sizeof(dbg), "[BuzzBridge64] Pipe connected on attempt %d\n", attempt + 1);
			OutputDebugStringA(dbg);
			break;
		}

		DWORD err = GetLastError();
		if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PIPE_BUSY) {
			snprintf(dbg, sizeof(dbg), "[BuzzBridge64] Pipe open failed with unexpected error %lu\n", err);
			OutputDebugStringA(dbg);
			break;
		}

		// Check if process died
		DWORD exitCode = 0;
		if (GetExitCodeProcess(hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
			snprintf(dbg, sizeof(dbg), "[BuzzBridge64] Host process exited with code %lu\n", exitCode);
			OutputDebugStringA(dbg);
			processRunning = false;
			CloseHandle(hProcess);
			hProcess = nullptr;
			return false;
		}

		Sleep(100);
	}

	if (hPipe == INVALID_HANDLE_VALUE) {
		OutputDebugStringA("[BuzzBridge64] Failed to connect to bridge pipe\n");
		Stop();
		return false;
	}

	DWORD mode = PIPE_READMODE_BYTE;
	SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr);
	pipe.SetHandle(hPipe);

	// Open shared memory
	std::string shmName = BridgeSharedMemName(sessionId);
	if (!sharedMem.Open(shmName, sizeof(BridgeSharedAudio))) {
		OutputDebugStringA("[BuzzBridge64] Failed to open shared memory\n");
		Stop();
		return false;
	}

	// Note: fast work path (event signaling) was removed because it causes
	// race conditions between Tick (pipe thread) and Work (event thread).
	// All commands go through the pipe which serializes them on one thread.

	// Verify connection with a ping
	if (!Ping()) {
		OutputDebugStringA("[BuzzBridge64] Ping failed\n");
		Stop();
		return false;
	}

	return true;
}

void BridgeClient::Stop()
{
	if (pipe.IsValid()) {
		// Try graceful shutdown
		pipe.SendCommand(kCmdShutdown);
		BridgeRespHeader resp;
		pipe.ReadResponse(resp); // best-effort
	}

	pipe.Close();
	sharedMem.Close();
	if (hWorkReady) { CloseHandle(hWorkReady); hWorkReady = nullptr; }
	if (hWorkDone) { CloseHandle(hWorkDone); hWorkDone = nullptr; }
	fastWorkEnabled = false;

	if (hProcess) {
		// Wait briefly for graceful exit
		if (WaitForSingleObject(hProcess, 2000) == WAIT_TIMEOUT) {
			TerminateProcess(hProcess, 1);
		}
		CloseHandle(hProcess);
		hProcess = nullptr;
	}

	processRunning = false;
}

bool BridgeClient::SendAndWaitOk(BridgeCommand cmd, const void* payload, uint32_t payloadSize)
{
	if (!pipe.IsValid()) return false;
	if (!pipe.SendCommand(cmd, payload, payloadSize)) return false;

	BridgeRespHeader resp;
	if (!pipe.ReadResponse(resp)) return false;
	return resp.resp == kRespOk;
}

bool BridgeClient::Ping()
{
	return SendAndWaitOk(kCmdPing);
}

bool BridgeClient::LoadDll(const std::string& dllPath)
{
	return SendAndWaitOk(kCmdLoadDll, dllPath.c_str(), (uint32_t)dllPath.size());
}

bool BridgeClient::InitMachine()
{
	return SendAndWaitOk(kCmdInitMachine);
}

bool BridgeClient::Unload()
{
	return SendAndWaitOk(kCmdUnload);
}

bool BridgeClient::SetMasterInfo(int bpm, int sampleRate, int ticksPerBeat)
{
	BridgeMasterInfo mi = {};
	mi.beatsPerMin = bpm;
	mi.samplesPerSec = sampleRate;
	mi.ticksPerBeat = ticksPerBeat;
	return SendAndWaitOk(kCmdSetMasterInfo, &mi, sizeof(mi));
}

bool BridgeClient::Tick(const std::vector<BridgeTickParam>& params)
{
	uint32_t size = (uint32_t)(params.size() * sizeof(BridgeTickParam));
	return SendAndWaitOk(kCmdTick, params.empty() ? nullptr : params.data(), size);
}

bool BridgeClient::Work(int numSamples, int workMode)
{
	// Always use pipe — fast work thread causes race conditions with Tick
	BridgeWorkCmd wcmd = {};
	wcmd.numSamples = numSamples;
	wcmd.workMode = workMode;
	return SendAndWaitOk(kCmdWork, &wcmd, sizeof(wcmd));
}

bool BridgeClient::StopMachine()
{
	return SendAndWaitOk(kCmdStop);
}

bool BridgeClient::SendMidiNote(int channel, int note, int velocity)
{
	BridgeMidiNote mn = {};
	mn.channel = channel;
	mn.note = note;
	mn.velocity = velocity;
	return SendAndWaitOk(kCmdMidiNote, &mn, sizeof(mn));
}

bool BridgeClient::SendMidiCC(int controller, int channel, int value)
{
	BridgeMidiCC cc = {};
	cc.controller = controller;
	cc.channel = channel;
	cc.value = value;
	return SendAndWaitOk(kCmdMidiCC, &cc, sizeof(cc));
}

bool BridgeClient::SetNumTracks(int numTracks)
{
	int32_t n = (int32_t)numTracks;
	return SendAndWaitOk(kCmdSetNumTracks, &n, sizeof(n));
}

bool BridgeClient::GetMachineInfo(BridgeMachineInfo& info, std::vector<BridgeParamInfo>& params)
{
	if (!pipe.IsValid()) return false;
	if (!pipe.SendCommand(kCmdGetInfo)) return false;

	BridgeRespHeader resp;
	if (!pipe.ReadResponse(resp)) return false;
	if (resp.resp != kRespMachineInfo) return false;

	if (!pipe.ReadAll(&info, sizeof(info))) return false;

	int totalParams = info.numGlobalParams + info.numTrackParams;
	params.resize(totalParams);
	if (totalParams > 0) {
		if (!pipe.ReadAll(params.data(), totalParams * sizeof(BridgeParamInfo))) return false;
	}

	return true;
}

BridgeSharedAudio* BridgeClient::GetAudio()
{
	return sharedMem.GetAudio();
}

} // namespace BuzzVst
