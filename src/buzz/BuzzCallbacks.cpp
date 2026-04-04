#include <windows.h>
#include "BuzzCallbacks.h"
#include <cstring>
#include <cstdio>

namespace BuzzVst {

BuzzCallbacks::BuzzCallbacks()
{
	InitializeCriticalSection(&cs);
	memset(auxBuffer, 0, sizeof(auxBuffer));
}

BuzzCallbacks::~BuzzCallbacks()
{
	if (mdkStub) { DestroyMDKStub(mdkStub); mdkStub = nullptr; }
	DeleteCriticalSection(&cs);
}

CWaveInfo const *BuzzCallbacks::GetWave(int const i) {
	if (waveTable) return waveTable->GetWave(i);
	return nullptr;
}
CWaveLevel const *BuzzCallbacks::GetWaveLevel(int const i, int const level) {
	if (waveTable) return waveTable->GetWaveLevel(i, level);
	return nullptr;
}

void BuzzCallbacks::MessageBox(char const *txt)
{
	if (txt) {
		OutputDebugStringA("[BuzzBridgeHost32] ");
		OutputDebugStringA(txt);
		OutputDebugStringA("\n");
	}
}

void BuzzCallbacks::Lock() { EnterCriticalSection(&cs); }
void BuzzCallbacks::Unlock() { LeaveCriticalSection(&cs); }

int BuzzCallbacks::GetWritePos() { return audioFrame; }
int BuzzCallbacks::GetPlayPos() { return audioFrame; }

float *BuzzCallbacks::GetAuxBuffer() { return auxBuffer; }
void BuzzCallbacks::ClearAuxBuffer() { memset(auxBuffer, 0, sizeof(auxBuffer)); }

int BuzzCallbacks::GetFreeWave() {
	if (waveTable) return waveTable->GetFreeWave();
	return 0;
}
bool BuzzCallbacks::AllocateWave(int const i, int const size, char const *name) { return false; }
void BuzzCallbacks::ScheduleEvent(int const time, dword const data) {}
void BuzzCallbacks::MidiOut(int const dev, dword const data) {}

short const *BuzzCallbacks::GetOscillatorTable(int const waveform)
{
	return BuzzOscTables::GetTable(waveform);
}

int BuzzCallbacks::GetEnvSize(int const wave, int const env) { return 0; }
bool BuzzCallbacks::GetEnvPoint(int const wave, int const env, int const i, word &x, word &y, int &flags) { return false; }

CWaveLevel const *BuzzCallbacks::GetNearestWaveLevel(int const i, int const note)
{
	// Special check: GetNearestWaveLevel(-1, -1) returns an MDK interface object.
	// MDK-based machines (e.g., ld grain) call this during initialization to get
	// a helper object they use for Init and Work delegation.
	if (i == -1 && note == -1) {
		if (!mdkStub) mdkStub = CreateMDKStub();
		isMDKMachine = true;
		OutputDebugStringA("[BuzzBridgeHost32] GetNearestWaveLevel(-1,-1): returning MDK stub (MDK machine detected)\n");
		return (CWaveLevel const*)mdkStub;
	}
	// Special check: GetNearestWaveLevel(-2, -2) is used to detect host version support
	if (i == -2 && note == -2) {
		return (CWaveLevel const*)1;
	}
	if (waveTable) return waveTable->GetNearestWaveLevel(i, note);
	return nullptr;
}

void BuzzCallbacks::SetNumberOfTracks(int const n) {}
CPattern *BuzzCallbacks::CreatePattern(char const *name, int const length) { return nullptr; }
CPattern *BuzzCallbacks::GetPattern(int const index) { return nullptr; }
char const *BuzzCallbacks::GetPatternName(CPattern *ppat) { return ""; }
void BuzzCallbacks::RenamePattern(char const *oldname, char const *newname) {}
void BuzzCallbacks::DeletePattern(CPattern *ppat) {}
int BuzzCallbacks::GetPatternData(CPattern *ppat, int const row, int const group, int const track, int const field) { return 0; }
void BuzzCallbacks::SetPatternData(CPattern *ppat, int const row, int const group, int const track, int const field, int const value) {}

CSequence *BuzzCallbacks::CreateSequence() { return nullptr; }
void BuzzCallbacks::DeleteSequence(CSequence *pseq) {}
CPattern *BuzzCallbacks::GetSequenceData(int const row) { return nullptr; }
void BuzzCallbacks::SetSequenceData(int const row, CPattern *ppat) {}

void BuzzCallbacks::SetMachineInterfaceEx(CMachineInterfaceEx *pex) { machineInterfaceEx = pex; }
void BuzzCallbacks::ControlChange__obsolete__(int group, int track, int param, int value) {}

int BuzzCallbacks::ADGetnumChannels(bool input) { return 0; }
void BuzzCallbacks::ADWrite(int channel, float *psamples, int numsamples) {}
void BuzzCallbacks::ADRead(int channel, float *psamples, int numsamples) {}

CMachine *BuzzCallbacks::GetThisMachine() { return (CMachine*)this; }
void BuzzCallbacks::ControlChange(CMachine *pmac, int group, int track, int param, int value) {}

CSequence *BuzzCallbacks::GetPlayingSequence(CMachine *pmac) { return nullptr; }
void *BuzzCallbacks::GetPlayingRow(CSequence *pseq, int group, int track) { return nullptr; }

int BuzzCallbacks::GetStateFlags() { return SF_PLAYING; }

void BuzzCallbacks::SetnumOutputChannels(CMachine *pmac, int n) {}
void BuzzCallbacks::SetEventHandler(CMachine *pmac, BEventType et, EVENT_HANDLER_PTR p, void *param) {}
char const *BuzzCallbacks::GetWaveName(int const i) {
	if (waveTable) return waveTable->GetWaveName(i);
	return "";
}
void BuzzCallbacks::SetInternalWaveName(CMachine *pmac, int const i, char const *name) {}
void BuzzCallbacks::GetMachineNames(CMachineDataOutput *pout) {}
CMachine *BuzzCallbacks::GetMachine(char const *name) { return nullptr; }
CMachineInfo const *BuzzCallbacks::GetMachineInfo(CMachine *pmac) { return machineInfo; }
char const *BuzzCallbacks::GetMachineName(CMachine *pmac) { return "BuzzVst"; }
bool BuzzCallbacks::GetInput(int index, float *psamples, int numsamples, bool stereo, float *extrabuffer) { return false; }

int BuzzCallbacks::GetHostVersion() { return MI_VERSION; }
int BuzzCallbacks::GetSongPosition() { return 0; }
void BuzzCallbacks::SetSongPosition(int pos) {}
int BuzzCallbacks::GetTempo() { return masterInfoPtr ? masterInfoPtr->BeatsPerMin : 125; }
void BuzzCallbacks::SetTempo(int bpm) {}
int BuzzCallbacks::GetTPB() { return masterInfoPtr ? masterInfoPtr->TicksPerBeat : 4; }
void BuzzCallbacks::SetTPB(int tpb) {}
int BuzzCallbacks::GetLoopStart() { return 0; }
int BuzzCallbacks::GetLoopEnd() { return 16; }
int BuzzCallbacks::GetSongEnd() { return 16; }
void BuzzCallbacks::Play() {}
void BuzzCallbacks::Stop() {}
bool BuzzCallbacks::RenameMachine(CMachine *pmac, char const *name) { return false; }
void BuzzCallbacks::SetModifiedFlag() {}
int BuzzCallbacks::GetAudioFrame() { return audioFrame; }
bool BuzzCallbacks::HostMIDIFiltering() { return true; }
dword BuzzCallbacks::GetThemeColor(char const *name) { return 0; }
void BuzzCallbacks::WriteProfileInt(char const *entry, int value) {}
void BuzzCallbacks::WriteProfileString(char const *entry, char const *value) {}
void BuzzCallbacks::WriteProfileBinary(char const *entry, byte *data, int nbytes) {}
int BuzzCallbacks::GetProfileInt(char const *entry, int defvalue) { return defvalue; }
void BuzzCallbacks::GetProfileString(char const *entry, char const *value, char const *defvalue) {}
void BuzzCallbacks::GetProfileBinary(char const *entry, byte **data, int *nbytes) { if (data) *data = nullptr; if (nbytes) *nbytes = 0; }
void BuzzCallbacks::FreeProfileBinary(byte *data) {}
int BuzzCallbacks::GetNumTracks(CMachine *pmac) { return 1; }
void BuzzCallbacks::SetNumTracks(CMachine *pmac, int n) {}
void BuzzCallbacks::SetPatternEditorStatusText(int pane, char const *text) {}
char const *BuzzCallbacks::DescribeValue(CMachine *pmac, int const param, int const value) { return nullptr; }
int BuzzCallbacks::GetBaseOctave() { return 4; }
int BuzzCallbacks::GetSelectedWave() { return 0; }
void BuzzCallbacks::SelectWave(int i) {}
void BuzzCallbacks::SetPatternLength(CPattern *p, int length) {}
int BuzzCallbacks::GetParameterState(CMachine *pmac, int group, int track, int param) { return 0; }
void BuzzCallbacks::ShowMachineWindow(CMachine *pmac, bool show) {}
void BuzzCallbacks::SetPatternEditorMachine(CMachine *pmac, bool gotoeditor) {}
CSubTickInfo const *BuzzCallbacks::GetSubTickInfo() { return nullptr; }
int BuzzCallbacks::GetSequenceColumn(CSequence *s) { return -1; }
void BuzzCallbacks::SetGroovePattern(float *data, int size) {}
void BuzzCallbacks::ControlChangeImmediate(CMachine *pmac, int group, int track, int param, int value) {}
void BuzzCallbacks::SendControlChanges(CMachine *pmac) {}
int BuzzCallbacks::GetAttribute(CMachine *pmac, int index) { return 0; }
void BuzzCallbacks::SetAttribute(CMachine *pmac, int index, int value) {}
void BuzzCallbacks::AttributesChanged(CMachine *pmac) {}
void BuzzCallbacks::GetMachinePosition(CMachine *pmac, float &x, float &y) { x = 0; y = 0; }
void BuzzCallbacks::SetMachinePosition(CMachine *pmac, float x, float y) {}
void BuzzCallbacks::MuteMachine(CMachine *pmac, bool mute) {}
void BuzzCallbacks::SoloMachine(CMachine *pmac) {}
void BuzzCallbacks::UpdateParameterDisplays(CMachine *pmac) {}

void BuzzCallbacks::WriteLine(char const *text)
{
	if (text) {
		OutputDebugStringA("[BuzzBridgeHost32] ");
		OutputDebugStringA(text);
		OutputDebugStringA("\n");
	}
}

bool BuzzCallbacks::GetOption(char const *name) { return false; }
bool BuzzCallbacks::GetPlayNotesState() { return false; }
void BuzzCallbacks::EnableMultithreading(bool enable) {}
CPattern *BuzzCallbacks::GetPatternByName(CMachine *pmac, char const *patname) { return nullptr; }
void BuzzCallbacks::SetPatternName(CPattern *p, char const *name) {}
int BuzzCallbacks::GetPatternLength(CPattern *p) { return 0; }
CMachine *BuzzCallbacks::GetPatternOwner(CPattern *p) { return nullptr; }
bool BuzzCallbacks::MachineImplementsFunction(CMachine *pmac, int vtblindex, bool miex) { return false; }
void BuzzCallbacks::SendMidiNote(CMachine *pmac, int const channel, int const value, int const velocity) {}
void BuzzCallbacks::SendMidiControlChange(CMachine *pmac, int const ctrl, int const channel, int const value) {}
int BuzzCallbacks::GetBuildNumber() { return 1503; }
void BuzzCallbacks::SetMidiFocus(CMachine *pmac) {}
void BuzzCallbacks::BeginWriteToPlayingPattern(CMachine *pmac, int quantization, CPatternWriteInfo &outpwi) {}
void BuzzCallbacks::WriteToPlayingPattern(CMachine *pmac, int group, int track, int param, int value) {}
void BuzzCallbacks::EndWriteToPlayingPattern(CMachine *pmac) {}
void *BuzzCallbacks::GetMainWindow() { return nullptr; }
void BuzzCallbacks::DebugLock(char const *sourcelocation) { Lock(); }
void BuzzCallbacks::SetInputChannelCount(int count) {}
void BuzzCallbacks::SetOutputChannelCount(int count) {}
bool BuzzCallbacks::IsSongClosing() { return false; }
void BuzzCallbacks::SetMidiInputMode(MidiInputMode mode) {}
int BuzzCallbacks::RemapLoadedMachineParameterIndex(CMachine *pmac, int i) { return -1; }
char const *BuzzCallbacks::GetThemePath() { return ""; }
void BuzzCallbacks::InvalidateParameterValueDescription(CMachine *pmac, int index) {}
void BuzzCallbacks::RemapLoadedMachineName(char *name, int bufsize) {}
bool BuzzCallbacks::IsMachineMuted(CMachine *pmac) { return false; }
int BuzzCallbacks::GetInputChannelConnectionCount(CMachine *pmac, int channel) { return 0; }
int BuzzCallbacks::GetOutputChannelConnectionCount(CMachine *pmac, int channel) { return 0; }
void BuzzCallbacks::ToggleRecordMode() {}
int BuzzCallbacks::GetSequenceCount(CMachine *pmac) { return 0; }
CSequence *BuzzCallbacks::GetSequence(CMachine *pmac, int index) { return nullptr; }
CPattern *BuzzCallbacks::GetPlayingPattern(CSequence *pseq) { return nullptr; }
int BuzzCallbacks::GetPlayingPatternPosition(CSequence *pseq) { return -1; }
bool BuzzCallbacks::IsValidAsciiChar(CMachine *pmac, int param, char ch) { return true; }
int BuzzCallbacks::GetConnectionCount(CMachine *pmac, bool output) { return 0; }
CMachineConnection *BuzzCallbacks::GetConnection(CMachine *pmac, bool output, int index) { return nullptr; }
CMachine *BuzzCallbacks::GetConnectionSource(CMachineConnection *pmc, int &channel) { channel = 0; return nullptr; }
CMachine *BuzzCallbacks::GetConnectionDestination(CMachineConnection *pmc, int &channel) { channel = 0; return nullptr; }
int BuzzCallbacks::GetTotalLatency() { return 0; }
void *BuzzCallbacks::GetMachineModuleHandle(CMachine *pmac) { return nullptr; }

} // namespace BuzzVst
