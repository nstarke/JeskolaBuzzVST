// Stub implementations for the base CMICallbacks, CMachineDataInput, and
// CMachineDataOutput virtual methods. These are normally provided by the
// Buzz host runtime. We provide empty stubs so the linker resolves them.
// Our BuzzCallbacks subclass overrides all of these.

#include <windows.h>
#include "MachineInterface.h"

// CMICallbacks stubs - these should never be called (always overridden)
CWaveInfo const *CMICallbacks::GetWave(int const i) { return nullptr; }
CWaveLevel const *CMICallbacks::GetWaveLevel(int const i, int const level) { return nullptr; }
void CMICallbacks::MessageBox(char const *txt) {}
void CMICallbacks::Lock() {}
void CMICallbacks::Unlock() {}
int CMICallbacks::GetWritePos() { return 0; }
int CMICallbacks::GetPlayPos() { return 0; }
float *CMICallbacks::GetAuxBuffer() { return nullptr; }
void CMICallbacks::ClearAuxBuffer() {}
int CMICallbacks::GetFreeWave() { return 0; }
bool CMICallbacks::AllocateWave(int const i, int const size, char const *name) { return false; }
void CMICallbacks::ScheduleEvent(int const time, dword const data) {}
void CMICallbacks::MidiOut(int const dev, dword const data) {}
short const *CMICallbacks::GetOscillatorTable(int const waveform) { return nullptr; }
int CMICallbacks::GetEnvSize(int const wave, int const env) { return 0; }
bool CMICallbacks::GetEnvPoint(int const wave, int const env, int const i, word &x, word &y, int &flags) { return false; }
CWaveLevel const *CMICallbacks::GetNearestWaveLevel(int const i, int const note) { return nullptr; }
void CMICallbacks::SetNumberOfTracks(int const n) {}
CPattern *CMICallbacks::CreatePattern(char const *name, int const length) { return nullptr; }
CPattern *CMICallbacks::GetPattern(int const index) { return nullptr; }
char const *CMICallbacks::GetPatternName(CPattern *ppat) { return ""; }
void CMICallbacks::RenamePattern(char const *oldname, char const *newname) {}
void CMICallbacks::DeletePattern(CPattern *ppat) {}
int CMICallbacks::GetPatternData(CPattern *ppat, int const row, int const group, int const track, int const field) { return 0; }
void CMICallbacks::SetPatternData(CPattern *ppat, int const row, int const group, int const track, int const field, int const value) {}
CSequence *CMICallbacks::CreateSequence() { return nullptr; }
void CMICallbacks::DeleteSequence(CSequence *pseq) {}
CPattern *CMICallbacks::GetSequenceData(int const row) { return nullptr; }
void CMICallbacks::SetSequenceData(int const row, CPattern *ppat) {}
void CMICallbacks::SetMachineInterfaceEx(CMachineInterfaceEx *pex) {}
void CMICallbacks::ControlChange__obsolete__(int group, int track, int param, int value) {}
int CMICallbacks::ADGetnumChannels(bool input) { return 0; }
void CMICallbacks::ADWrite(int channel, float *psamples, int numsamples) {}
void CMICallbacks::ADRead(int channel, float *psamples, int numsamples) {}
CMachine *CMICallbacks::GetThisMachine() { return nullptr; }
void CMICallbacks::ControlChange(CMachine *pmac, int group, int track, int param, int value) {}
CSequence *CMICallbacks::GetPlayingSequence(CMachine *pmac) { return nullptr; }
void *CMICallbacks::GetPlayingRow(CSequence *pseq, int group, int track) { return nullptr; }
int CMICallbacks::GetStateFlags() { return 0; }
void CMICallbacks::SetnumOutputChannels(CMachine *pmac, int n) {}
void CMICallbacks::SetEventHandler(CMachine *pmac, BEventType et, EVENT_HANDLER_PTR p, void *param) {}
char const *CMICallbacks::GetWaveName(int const i) { return ""; }
void CMICallbacks::SetInternalWaveName(CMachine *pmac, int const i, char const *name) {}
void CMICallbacks::GetMachineNames(CMachineDataOutput *pout) {}
CMachine *CMICallbacks::GetMachine(char const *name) { return nullptr; }
CMachineInfo const *CMICallbacks::GetMachineInfo(CMachine *pmac) { return nullptr; }
char const *CMICallbacks::GetMachineName(CMachine *pmac) { return ""; }
bool CMICallbacks::GetInput(int index, float *psamples, int numsamples, bool stereo, float *extrabuffer) { return false; }
int CMICallbacks::GetHostVersion() { return 0; }
int CMICallbacks::GetSongPosition() { return 0; }
void CMICallbacks::SetSongPosition(int pos) {}
int CMICallbacks::GetTempo() { return 125; }
void CMICallbacks::SetTempo(int bpm) {}
int CMICallbacks::GetTPB() { return 4; }
void CMICallbacks::SetTPB(int tpb) {}
int CMICallbacks::GetLoopStart() { return 0; }
int CMICallbacks::GetLoopEnd() { return 0; }
int CMICallbacks::GetSongEnd() { return 0; }
void CMICallbacks::Play() {}
void CMICallbacks::Stop() {}
bool CMICallbacks::RenameMachine(CMachine *pmac, char const *name) { return false; }
void CMICallbacks::SetModifiedFlag() {}
int CMICallbacks::GetAudioFrame() { return 0; }
bool CMICallbacks::HostMIDIFiltering() { return false; }
dword CMICallbacks::GetThemeColor(char const *name) { return 0; }
void CMICallbacks::WriteProfileInt(char const *entry, int value) {}
void CMICallbacks::WriteProfileString(char const *entry, char const *value) {}
void CMICallbacks::WriteProfileBinary(char const *entry, byte *data, int nbytes) {}
int CMICallbacks::GetProfileInt(char const *entry, int defvalue) { return defvalue; }
void CMICallbacks::GetProfileString(char const *entry, char const *value, char const *defvalue) {}
void CMICallbacks::GetProfileBinary(char const *entry, byte **data, int *nbytes) { if (data) *data = nullptr; if (nbytes) *nbytes = 0; }
void CMICallbacks::FreeProfileBinary(byte *data) {}
int CMICallbacks::GetNumTracks(CMachine *pmac) { return 0; }
void CMICallbacks::SetNumTracks(CMachine *pmac, int n) {}
void CMICallbacks::SetPatternEditorStatusText(int pane, char const *text) {}
char const *CMICallbacks::DescribeValue(CMachine *pmac, int const param, int const value) { return nullptr; }
int CMICallbacks::GetBaseOctave() { return 4; }
int CMICallbacks::GetSelectedWave() { return 0; }
void CMICallbacks::SelectWave(int i) {}
void CMICallbacks::SetPatternLength(CPattern *p, int length) {}
int CMICallbacks::GetParameterState(CMachine *pmac, int group, int track, int param) { return 0; }
void CMICallbacks::ShowMachineWindow(CMachine *pmac, bool show) {}
void CMICallbacks::SetPatternEditorMachine(CMachine *pmac, bool gotoeditor) {}
CSubTickInfo const *CMICallbacks::GetSubTickInfo() { return nullptr; }
int CMICallbacks::GetSequenceColumn(CSequence *s) { return -1; }
void CMICallbacks::SetGroovePattern(float *data, int size) {}
void CMICallbacks::ControlChangeImmediate(CMachine *pmac, int group, int track, int param, int value) {}
void CMICallbacks::SendControlChanges(CMachine *pmac) {}
int CMICallbacks::GetAttribute(CMachine *pmac, int index) { return 0; }
void CMICallbacks::SetAttribute(CMachine *pmac, int index, int value) {}
void CMICallbacks::AttributesChanged(CMachine *pmac) {}
void CMICallbacks::GetMachinePosition(CMachine *pmac, float &x, float &y) { x = 0; y = 0; }
void CMICallbacks::SetMachinePosition(CMachine *pmac, float x, float y) {}
void CMICallbacks::MuteMachine(CMachine *pmac, bool mute) {}
void CMICallbacks::SoloMachine(CMachine *pmac) {}
void CMICallbacks::UpdateParameterDisplays(CMachine *pmac) {}
void CMICallbacks::WriteLine(char const *text) {}
bool CMICallbacks::GetOption(char const *name) { return false; }
bool CMICallbacks::GetPlayNotesState() { return false; }
void CMICallbacks::EnableMultithreading(bool enable) {}
CPattern *CMICallbacks::GetPatternByName(CMachine *pmac, char const *patname) { return nullptr; }
void CMICallbacks::SetPatternName(CPattern *p, char const *name) {}
int CMICallbacks::GetPatternLength(CPattern *p) { return 0; }
CMachine *CMICallbacks::GetPatternOwner(CPattern *p) { return nullptr; }
bool CMICallbacks::MachineImplementsFunction(CMachine *pmac, int vtblindex, bool miex) { return false; }
void CMICallbacks::SendMidiNote(CMachine *pmac, int const channel, int const value, int const velocity) {}
void CMICallbacks::SendMidiControlChange(CMachine *pmac, int const ctrl, int const channel, int const value) {}
int CMICallbacks::GetBuildNumber() { return 0; }
void CMICallbacks::SetMidiFocus(CMachine *pmac) {}
void CMICallbacks::BeginWriteToPlayingPattern(CMachine *pmac, int quantization, CPatternWriteInfo &outpwi) {}
void CMICallbacks::WriteToPlayingPattern(CMachine *pmac, int group, int track, int param, int value) {}
void CMICallbacks::EndWriteToPlayingPattern(CMachine *pmac) {}
void *CMICallbacks::GetMainWindow() { return nullptr; }
void CMICallbacks::DebugLock(char const *sourcelocation) {}
void CMICallbacks::SetInputChannelCount(int count) {}
void CMICallbacks::SetOutputChannelCount(int count) {}
bool CMICallbacks::IsSongClosing() { return false; }
void CMICallbacks::SetMidiInputMode(MidiInputMode mode) {}
int CMICallbacks::RemapLoadedMachineParameterIndex(CMachine *pmac, int i) { return -1; }
char const *CMICallbacks::GetThemePath() { return ""; }
void CMICallbacks::InvalidateParameterValueDescription(CMachine *pmac, int index) {}
void CMICallbacks::RemapLoadedMachineName(char *name, int bufsize) {}
bool CMICallbacks::IsMachineMuted(CMachine *pmac) { return false; }
int CMICallbacks::GetInputChannelConnectionCount(CMachine *pmac, int channel) { return 0; }
int CMICallbacks::GetOutputChannelConnectionCount(CMachine *pmac, int channel) { return 0; }
void CMICallbacks::ToggleRecordMode() {}
int CMICallbacks::GetSequenceCount(CMachine *pmac) { return 0; }
CSequence *CMICallbacks::GetSequence(CMachine *pmac, int index) { return nullptr; }
CPattern *CMICallbacks::GetPlayingPattern(CSequence *pseq) { return nullptr; }
int CMICallbacks::GetPlayingPatternPosition(CSequence *pseq) { return -1; }
bool CMICallbacks::IsValidAsciiChar(CMachine *pmac, int param, char ch) { return true; }
int CMICallbacks::GetConnectionCount(CMachine *pmac, bool output) { return 0; }
CMachineConnection *CMICallbacks::GetConnection(CMachine *pmac, bool output, int index) { return nullptr; }
CMachine *CMICallbacks::GetConnectionSource(CMachineConnection *pmc, int &channel) { channel = 0; return nullptr; }
CMachine *CMICallbacks::GetConnectionDestination(CMachineConnection *pmc, int &channel) { channel = 0; return nullptr; }
int CMICallbacks::GetTotalLatency() { return 0; }
void *CMICallbacks::GetMachineModuleHandle(CMachine *pmac) { return nullptr; }

// CMachineDataInput / CMachineDataOutput stubs
void CMachineDataInput::Read(void *pbuf, int const numbytes) {}
void CMachineDataOutput::Write(void *pbuf, int const numbytes) {}
