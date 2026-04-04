#pragma once

#include <windows.h>
#include "MachineInterface.h"
#include "BuzzOscTables.h"
#include "BuzzWaveTable.h"
#include "BuzzMDKHelper.h"

namespace BuzzVst {

// Stub implementation of CMICallbacks that provides the minimum
// functionality needed for Buzz machines to operate outside of Buzz.
// The vtable order MUST match MachineInterface.h exactly.
class BuzzCallbacks : public CMICallbacks {
public:
	BuzzCallbacks();
	~BuzzCallbacks();

	// Wave-related - return null/safe defaults
	CWaveInfo const *GetWave(int const i) override;
	CWaveLevel const *GetWaveLevel(int const i, int const level) override;
	void MessageBox(char const *txt) override;
	void Lock() override;
	void Unlock() override;
	int GetWritePos() override;
	int GetPlayPos() override;
	float *GetAuxBuffer() override;
	void ClearAuxBuffer() override;
	int GetFreeWave() override;
	bool AllocateWave(int const i, int const size, char const *name) override;
	void ScheduleEvent(int const time, dword const data) override;
	void MidiOut(int const dev, dword const data) override;
	short const *GetOscillatorTable(int const waveform) override;

	int GetEnvSize(int const wave, int const env) override;
	bool GetEnvPoint(int const wave, int const env, int const i, word &x, word &y, int &flags) override;

	CWaveLevel const *GetNearestWaveLevel(int const i, int const note) override;

	void SetNumberOfTracks(int const n) override;
	CPattern *CreatePattern(char const *name, int const length) override;
	CPattern *GetPattern(int const index) override;
	char const *GetPatternName(CPattern *ppat) override;
	void RenamePattern(char const *oldname, char const *newname) override;
	void DeletePattern(CPattern *ppat) override;
	int GetPatternData(CPattern *ppat, int const row, int const group, int const track, int const field) override;
	void SetPatternData(CPattern *ppat, int const row, int const group, int const track, int const field, int const value) override;

	CSequence *CreateSequence() override;
	void DeleteSequence(CSequence *pseq) override;
	CPattern *GetSequenceData(int const row) override;
	void SetSequenceData(int const row, CPattern *ppat) override;

	void SetMachineInterfaceEx(CMachineInterfaceEx *pex) override;
	void ControlChange__obsolete__(int group, int track, int param, int value) override;

	int ADGetnumChannels(bool input) override;
	void ADWrite(int channel, float *psamples, int numsamples) override;
	void ADRead(int channel, float *psamples, int numsamples) override;

	CMachine *GetThisMachine() override;
	void ControlChange(CMachine *pmac, int group, int track, int param, int value) override;

	CSequence *GetPlayingSequence(CMachine *pmac) override;
	void *GetPlayingRow(CSequence *pseq, int group, int track) override;
	int GetStateFlags() override;
	void SetnumOutputChannels(CMachine *pmac, int n) override;
	void SetEventHandler(CMachine *pmac, BEventType et, EVENT_HANDLER_PTR p, void *param) override;
	char const *GetWaveName(int const i) override;
	void SetInternalWaveName(CMachine *pmac, int const i, char const *name) override;
	void GetMachineNames(CMachineDataOutput *pout) override;
	CMachine *GetMachine(char const *name) override;
	CMachineInfo const *GetMachineInfo(CMachine *pmac) override;
	char const *GetMachineName(CMachine *pmac) override;
	bool GetInput(int index, float *psamples, int numsamples, bool stereo, float *extrabuffer) override;

	int GetHostVersion() override;
	int GetSongPosition() override;
	void SetSongPosition(int pos) override;
	int GetTempo() override;
	void SetTempo(int bpm) override;
	int GetTPB() override;
	void SetTPB(int tpb) override;
	int GetLoopStart() override;
	int GetLoopEnd() override;
	int GetSongEnd() override;
	void Play() override;
	void Stop() override;
	bool RenameMachine(CMachine *pmac, char const *name) override;
	void SetModifiedFlag() override;
	int GetAudioFrame() override;
	bool HostMIDIFiltering() override;
	dword GetThemeColor(char const *name) override;
	void WriteProfileInt(char const *entry, int value) override;
	void WriteProfileString(char const *entry, char const *value) override;
	void WriteProfileBinary(char const *entry, byte *data, int nbytes) override;
	int GetProfileInt(char const *entry, int defvalue) override;
	void GetProfileString(char const *entry, char const *value, char const *defvalue) override;
	void GetProfileBinary(char const *entry, byte **data, int *nbytes) override;
	void FreeProfileBinary(byte *data) override;
	int GetNumTracks(CMachine *pmac) override;
	void SetNumTracks(CMachine *pmac, int n) override;
	void SetPatternEditorStatusText(int pane, char const *text) override;
	char const *DescribeValue(CMachine *pmac, int const param, int const value) override;
	int GetBaseOctave() override;
	int GetSelectedWave() override;
	void SelectWave(int i) override;
	void SetPatternLength(CPattern *p, int length) override;
	int GetParameterState(CMachine *pmac, int group, int track, int param) override;
	void ShowMachineWindow(CMachine *pmac, bool show) override;
	void SetPatternEditorMachine(CMachine *pmac, bool gotoeditor) override;
	CSubTickInfo const *GetSubTickInfo() override;
	int GetSequenceColumn(CSequence *s) override;
	void SetGroovePattern(float *data, int size) override;
	void ControlChangeImmediate(CMachine *pmac, int group, int track, int param, int value) override;
	void SendControlChanges(CMachine *pmac) override;
	int GetAttribute(CMachine *pmac, int index) override;
	void SetAttribute(CMachine *pmac, int index, int value) override;
	void AttributesChanged(CMachine *pmac) override;
	void GetMachinePosition(CMachine *pmac, float &x, float &y) override;
	void SetMachinePosition(CMachine *pmac, float x, float y) override;
	void MuteMachine(CMachine *pmac, bool mute) override;
	void SoloMachine(CMachine *pmac) override;
	void UpdateParameterDisplays(CMachine *pmac) override;
	void WriteLine(char const *text) override;
	bool GetOption(char const *name) override;
	bool GetPlayNotesState() override;
	void EnableMultithreading(bool enable) override;
	CPattern *GetPatternByName(CMachine *pmac, char const *patname) override;
	void SetPatternName(CPattern *p, char const *name) override;
	int GetPatternLength(CPattern *p) override;
	CMachine *GetPatternOwner(CPattern *p) override;
	bool MachineImplementsFunction(CMachine *pmac, int vtblindex, bool miex) override;
	void SendMidiNote(CMachine *pmac, int const channel, int const value, int const velocity) override;
	void SendMidiControlChange(CMachine *pmac, int const ctrl, int const channel, int const value) override;
	int GetBuildNumber() override;
	void SetMidiFocus(CMachine *pmac) override;
	void BeginWriteToPlayingPattern(CMachine *pmac, int quantization, CPatternWriteInfo &outpwi) override;
	void WriteToPlayingPattern(CMachine *pmac, int group, int track, int param, int value) override;
	void EndWriteToPlayingPattern(CMachine *pmac) override;
	void *GetMainWindow() override;
	void DebugLock(char const *sourcelocation) override;
	void SetInputChannelCount(int count) override;
	void SetOutputChannelCount(int count) override;
	bool IsSongClosing() override;
	void SetMidiInputMode(MidiInputMode mode) override;
	int RemapLoadedMachineParameterIndex(CMachine *pmac, int i) override;
	char const *GetThemePath() override;
	void InvalidateParameterValueDescription(CMachine *pmac, int index) override;
	void RemapLoadedMachineName(char *name, int bufsize) override;
	bool IsMachineMuted(CMachine *pmac) override;
	int GetInputChannelConnectionCount(CMachine *pmac, int channel) override;
	int GetOutputChannelConnectionCount(CMachine *pmac, int channel) override;
	void ToggleRecordMode() override;
	int GetSequenceCount(CMachine *pmac) override;
	CSequence *GetSequence(CMachine *pmac, int index) override;
	CPattern *GetPlayingPattern(CSequence *pseq) override;
	int GetPlayingPatternPosition(CSequence *pseq) override;
	bool IsValidAsciiChar(CMachine *pmac, int param, char ch) override;
	int GetConnectionCount(CMachine *pmac, bool output) override;
	CMachineConnection *GetConnection(CMachine *pmac, bool output, int index) override;
	CMachine *GetConnectionSource(CMachineConnection *pmc, int &channel) override;
	CMachine *GetConnectionDestination(CMachineConnection *pmc, int &channel) override;
	int GetTotalLatency() override;
	void *GetMachineModuleHandle(CMachine *pmac) override;

	// Stored references (set by loader)
	const CMachineInfo* machineInfo = nullptr;
	CMasterInfo* masterInfoPtr = nullptr;

	// Extended machine interface - captured when machine calls SetMachineInterfaceEx
	CMachineInterfaceEx* machineInterfaceEx = nullptr;

	// Wave table - provides sample data to machines
	BuzzWaveTable* waveTable = nullptr;

	// MDK stub - returned by GetNearestWaveLevel(-1, -1) for MDK-based machines
	void* mdkStub = nullptr;

private:
	CRITICAL_SECTION cs;
	float auxBuffer[MAX_BUFFER_LENGTH * 2]; // stereo aux buffer
	int audioFrame = 0;
};

} // namespace BuzzVst
