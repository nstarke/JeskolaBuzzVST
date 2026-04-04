// Copyright (C) 1997-2014 Oskari Tammelin (ot@iki.fi)
// This header file may be used to write _freeware_ DLL "machines" for Buzz
// Using it for anything else is not allowed without a permission from the author

#ifndef __MACHINE_INTERFACE_H
#define __MACHINE_INTERFACE_H

#include <stdio.h>
#include <assert.h>
#include <string.h>

// Prevent Windows API macros from conflicting with Buzz API method names
#ifdef MessageBox
#undef MessageBox
#endif
#ifdef WriteProfileString
#undef WriteProfileString
#endif
#ifdef GetProfileInt
#undef GetProfileInt
#endif
#ifdef GetProfileString
#undef GetProfileString
#endif
#ifdef WriteProfileInt
#undef WriteProfileInt
#endif
#ifdef GetProfileBinary
#undef GetProfileBinary
#endif
#ifdef WriteProfileBinary
#undef WriteProfileBinary
#endif

#define MI_VERSION				66

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned long dword;

static double const BUZZ_PI = 3.14159265358979323846;

#define MAX_BUFFER_LENGTH		256

#define MT_MASTER				0
#define MT_GENERATOR			1
#define MT_EFFECT				2

#define NOTE_NO					0
#define NOTE_OFF				255
#define NOTE_MIN				1
#define NOTE_MAX				((16 * 9) + 12)
#define SWITCH_OFF				0
#define SWITCH_ON				1
#define SWITCH_NO				255
#define WAVE_MIN				1
#define WAVE_MAX				200
#define WAVE_NO					0

#define MPF_WAVE				1
#define MPF_STATE				2
#define MPF_TICK_ON_EDIT		4
#define MPF_TIE_TO_NEXT			8
#define MPF_ASCII				16

#define MIF_MONO_TO_STEREO		(1<<0)
#define MIF_PLAYS_WAVES			(1<<1)
#define MIF_USES_LIB_INTERFACE	(1<<2)
#define MIF_USES_INSTRUMENTS	(1<<3)
#define MIF_DOES_INPUT_MIXING	(1<<4)
#define MIF_NO_OUTPUT			(1<<5)
#define MIF_CONTROL_MACHINE		(1<<6)
#define MIF_INTERNAL_AUX		(1<<7)
#define MIF_EXTENDED_MENUS		(1<<8)
#define MIF_PATTERN_EDITOR		(1<<9)
#define MIF_PE_NO_CLIENT_EDGE	(1<<10)
#define MIF_GROOVE_CONTROL		(1<<11)
#define MIF_DRAW_PATTERN_BOX	(1<<12)
#define MIF_STEREO_EFFECT		(1<<13)
#define MIF_MULTI_IO			(1<<14)
#define MIF_PREFER_MIDI_NOTES	(1<<15)
#define MIF_LOAD_DATA_RUNTIME	(1<<16)
#define MIF_ALWAYS_SHOW_PLUGS	(1<<17)

#define WM_NOIO					0
#define WM_READ					1
#define WM_WRITE				2
#define WM_READWRITE			3

#define SF_PLAYING				1
#define SF_RECORDING			2

enum BEventType
{
	DoubleClickMachine,
	gDeleteMachine,
	gAddMachine,
	gRenameMachine,
	gUndeleteMachine,
	gWaveChanged
};

class CMachineInterface;
typedef bool (CMachineInterface::*EVENT_HANDLER_PTR)(void *);

enum CMPType { pt_note, pt_switch, pt_byte, pt_word, pt_internal=127 };

class CMachineParameter
{
public:
	CMPType Type;
	char const *Name;
	char const *Description;
	int MinValue;
	int MaxValue;
	int NoValue;
	int Flags;
	int DefValue;
};

class CMachineAttribute
{
public:
	char const *Name;
	int MinValue;
	int MaxValue;
	int DefValue;
};

class CMasterInfo
{
public:
	int BeatsPerMin;
	int TicksPerBeat;
	int SamplesPerSec;
	int SamplesPerTick;
	int PosInTick;
	float TicksPerSec;
	int GrooveSize;
	int PosInGroove;
	float *GrooveData;
};

class CSubTickInfo
{
public:
	int SubTicksPerTick;
	int CurrentSubTick;
	int SamplesPerSubTick;
	int PosInSubTick;
};

#define WF_LOOP			1
#define WF_NOT16BIT		4
#define WF_STEREO		8
#define WF_BIDIR_LOOP	16

class CWaveInfo
{
public:
	int Flags;
	float Volume;
};

class CWaveLevel
{
public:
	int numSamples;
	short *pSamples;
	int RootNote;
	int SamplesPerSec;
	int LoopStart;
	int LoopEnd;
};

#define OWF_SINE			0
#define OWF_SAWTOOTH		1
#define OWF_PULSE			2
#define OWF_TRIANGLE		3
#define OWF_NOISE			4
#define OWF_303_SAWTOOTH	5

inline int GetOscTblOffset(int const level)
{
	assert(level >= 0 && level <= 10);
	return (2048+1024+512+256+128+64+32+16+8+4) & ~((2048+1024+512+256+128+64+32+16+8+4) >> level);
}

class CPattern;
class CSequence;
class CMachineInterfaceEx;
class CMachine;
class CMachineConnection;
class CMachineDataOutput;
class CMachineInfo;

class CDrawPatternBoxContext
{
public:
	CPattern *Pattern;
	void *HDC;
	int Left;
	int Top;
	int Width;
	int Height;
	float TicksPerPixel;
};

class CPatternWriteInfo
{
public:
	int Row;
	float BuzzTickPosition;
};

enum MidiInputMode { MIM_Immediate, MIM_AudioThread, MIM_AudioThreadSubTick };

class CMICallbacks
{
public:
	virtual CWaveInfo const *GetWave(int const i);
	virtual CWaveLevel const *GetWaveLevel(int const i, int const level);
	virtual void MessageBox(char const *txt);
	virtual void Lock();
	virtual void Unlock();
	virtual int GetWritePos();
	virtual int GetPlayPos();
	virtual float *GetAuxBuffer();
	virtual void ClearAuxBuffer();
	virtual int GetFreeWave();
	virtual bool AllocateWave(int const i, int const size, char const *name);
	virtual void ScheduleEvent(int const time, dword const data);
	virtual void MidiOut(int const dev, dword const data);
	virtual short const *GetOscillatorTable(int const waveform);

	virtual int GetEnvSize(int const wave, int const env);
	virtual bool GetEnvPoint(int const wave, int const env, int const i, word &x, word &y, int &flags);

	virtual CWaveLevel const *GetNearestWaveLevel(int const i, int const note);

	virtual void SetNumberOfTracks(int const n);
	virtual CPattern *CreatePattern(char const *name, int const length);
	virtual CPattern *GetPattern(int const index);
	virtual char const *GetPatternName(CPattern *ppat);
	virtual void RenamePattern(char const *oldname, char const *newname);
	virtual void DeletePattern(CPattern *ppat);
	virtual int GetPatternData(CPattern *ppat, int const row, int const group, int const track, int const field);
	virtual void SetPatternData(CPattern *ppat, int const row, int const group, int const track, int const field, int const value);

	virtual CSequence *CreateSequence();
	virtual void DeleteSequence(CSequence *pseq);

	virtual CPattern *GetSequenceData(int const row);
	virtual void SetSequenceData(int const row, CPattern *ppat);

	virtual void SetMachineInterfaceEx(CMachineInterfaceEx *pex);
	virtual void ControlChange__obsolete__(int group, int track, int param, int value);

	virtual int ADGetnumChannels(bool input);
	virtual void ADWrite(int channel, float *psamples, int numsamples);
	virtual void ADRead(int channel, float *psamples, int numsamples);

	virtual CMachine *GetThisMachine();
	virtual void ControlChange(CMachine *pmac, int group, int track, int param, int value);

	virtual CSequence *GetPlayingSequence(CMachine *pmac);
	virtual void *GetPlayingRow(CSequence *pseq, int group, int track);
	virtual int GetStateFlags();
	virtual void SetnumOutputChannels(CMachine *pmac, int n);
	virtual void SetEventHandler(CMachine *pmac, BEventType et, EVENT_HANDLER_PTR p, void *param);
	virtual char const *GetWaveName(int const i);
	virtual void SetInternalWaveName(CMachine *pmac, int const i, char const *name);
	virtual void GetMachineNames(CMachineDataOutput *pout);
	virtual CMachine *GetMachine(char const *name);
	virtual CMachineInfo const *GetMachineInfo(CMachine *pmac);
	virtual char const *GetMachineName(CMachine *pmac);
	virtual bool GetInput(int index, float *psamples, int numsamples, bool stereo, float *extrabuffer);

	virtual int GetHostVersion();
	virtual int GetSongPosition();
	virtual void SetSongPosition(int pos);
	virtual int GetTempo();
	virtual void SetTempo(int bpm);
	virtual int GetTPB();
	virtual void SetTPB(int tpb);
	virtual int GetLoopStart();
	virtual int GetLoopEnd();
	virtual int GetSongEnd();
	virtual void Play();
	virtual void Stop();
	virtual bool RenameMachine(CMachine *pmac, char const *name);
	virtual void SetModifiedFlag();
	virtual int GetAudioFrame();
	virtual bool HostMIDIFiltering();
	virtual dword GetThemeColor(char const *name);
	virtual void WriteProfileInt(char const *entry, int value);
	virtual void WriteProfileString(char const *entry, char const *value);
	virtual void WriteProfileBinary(char const *entry, byte *data, int nbytes);
	virtual int GetProfileInt(char const *entry, int defvalue);
	virtual void GetProfileString(char const *entry, char const *value, char const *defvalue);
	virtual void GetProfileBinary(char const *entry, byte **data, int *nbytes);
	virtual void FreeProfileBinary(byte *data);
	virtual int GetNumTracks(CMachine *pmac);
	virtual void SetNumTracks(CMachine *pmac, int n);
	virtual void SetPatternEditorStatusText(int pane, char const *text);
	virtual char const *DescribeValue(CMachine *pmac, int const param, int const value);
	virtual int GetBaseOctave();
	virtual int GetSelectedWave();
	virtual void SelectWave(int i);
	virtual void SetPatternLength(CPattern *p, int length);
	virtual int GetParameterState(CMachine *pmac, int group, int track, int param);
	virtual void ShowMachineWindow(CMachine *pmac, bool show);
	virtual void SetPatternEditorMachine(CMachine *pmac, bool gotoeditor);
	virtual CSubTickInfo const *GetSubTickInfo();
	virtual int GetSequenceColumn(CSequence *s);
	virtual void SetGroovePattern(float *data, int size);
	virtual void ControlChangeImmediate(CMachine *pmac, int group, int track, int param, int value);
	virtual void SendControlChanges(CMachine *pmac);
	virtual int GetAttribute(CMachine *pmac, int index);
	virtual void SetAttribute(CMachine *pmac, int index, int value);
	virtual void AttributesChanged(CMachine *pmac);
	virtual void GetMachinePosition(CMachine *pmac, float &x, float &y);
	virtual void SetMachinePosition(CMachine *pmac, float x, float y);
	virtual void MuteMachine(CMachine *pmac, bool mute);
	virtual void SoloMachine(CMachine *pmac);
	virtual void UpdateParameterDisplays(CMachine *pmac);
	virtual void WriteLine(char const *text);
	virtual bool GetOption(char const *name);
	virtual bool GetPlayNotesState();
	virtual void EnableMultithreading(bool enable);
	virtual CPattern *GetPatternByName(CMachine *pmac, char const *patname);
	virtual void SetPatternName(CPattern *p, char const *name);
	virtual int GetPatternLength(CPattern *p);
	virtual CMachine *GetPatternOwner(CPattern *p);
	virtual bool MachineImplementsFunction(CMachine *pmac, int vtblindex, bool miex);
	virtual void SendMidiNote(CMachine *pmac, int const channel, int const value, int const velocity);
	virtual void SendMidiControlChange(CMachine *pmac, int const ctrl, int const channel, int const value);
	virtual int GetBuildNumber();
	virtual void SetMidiFocus(CMachine *pmac);
	virtual void BeginWriteToPlayingPattern(CMachine *pmac, int quantization, CPatternWriteInfo &outpwi);
	virtual void WriteToPlayingPattern(CMachine *pmac, int group, int track, int param, int value);
	virtual void EndWriteToPlayingPattern(CMachine *pmac);
	virtual void *GetMainWindow();
	virtual void DebugLock(char const *sourcelocation);
	virtual void SetInputChannelCount(int count);
	virtual void SetOutputChannelCount(int count);
	virtual bool IsSongClosing();
	virtual void SetMidiInputMode(MidiInputMode mode);
	virtual int RemapLoadedMachineParameterIndex(CMachine *pmac, int i);
	virtual char const *GetThemePath();
	virtual void InvalidateParameterValueDescription(CMachine *pmac, int index);
	virtual void RemapLoadedMachineName(char *name, int bufsize);
	virtual bool IsMachineMuted(CMachine *pmac);
	virtual int GetInputChannelConnectionCount(CMachine *pmac, int channel);
	virtual int GetOutputChannelConnectionCount(CMachine *pmac, int channel);
	virtual void ToggleRecordMode();
	virtual int GetSequenceCount(CMachine *pmac);
	virtual CSequence *GetSequence(CMachine *pmac, int index);
	virtual CPattern *GetPlayingPattern(CSequence *pseq);
	virtual int GetPlayingPatternPosition(CSequence *pseq);
	virtual bool IsValidAsciiChar(CMachine *pmac, int param, char ch);
	virtual int GetConnectionCount(CMachine *pmac, bool output);
	virtual CMachineConnection *GetConnection(CMachine *pmac, bool output, int index);
	virtual CMachine *GetConnectionSource(CMachineConnection *pmc, int &channel);
	virtual CMachine *GetConnectionDestination(CMachineConnection *pmc, int &channel);
	virtual int GetTotalLatency();
	virtual void *GetMachineModuleHandle(CMachine *pmac);
};

class CLibInterface
{
public:
	virtual void GetInstrumentList(CMachineDataOutput *pout) {}
	virtual bool GetInstrumentPath(char const *instrname, char *buf, int bufsize) { return false; }
	virtual void Dummy1() {} virtual void Dummy2() {} virtual void Dummy3() {} virtual void Dummy4() {}
	virtual void Dummy5() {} virtual void Dummy6() {} virtual void Dummy7() {} virtual void Dummy8() {}
	virtual void Dummy9() {} virtual void Dummy10() {} virtual void Dummy11() {} virtual void Dummy12() {}
	virtual void Dummy13() {} virtual void Dummy14() {} virtual void Dummy15() {} virtual void Dummy16() {}
	virtual void Dummy17() {} virtual void Dummy18() {} virtual void Dummy19() {} virtual void Dummy20() {}
	virtual void Dummy21() {} virtual void Dummy22() {} virtual void Dummy23() {} virtual void Dummy24() {}
	virtual void Dummy25() {} virtual void Dummy26() {} virtual void Dummy27() {} virtual void Dummy28() {}
	virtual void Dummy29() {} virtual void Dummy30() {} virtual void Dummy31() {} virtual void Dummy32() {}
};

class CMachineInfo
{
public:
	int Type;
	int Version;
	int Flags;
	int minTracks;
	int maxTracks;
	int numGlobalParameters;
	int numTrackParameters;
	CMachineParameter const **Parameters;
	int numAttributes;
	CMachineAttribute const **Attributes;
	char const *Name;
	char const *ShortName;
	char const *Author;
	char const *Commands;
	CLibInterface *pLI;
};

class CMachineDataInput
{
public:
	virtual void Read(void *pbuf, int const numbytes);

	void Read(int &d) { Read(&d, sizeof(int)); }
	void Read(dword &d) { Read(&d, sizeof(dword)); }
	void Read(short &d) { Read(&d, sizeof(short)); }
	void Read(word &d) { Read(&d, sizeof(word)); }
	void Read(char &d) { Read(&d, sizeof(char)); }
	void Read(byte &d) { Read(&d, sizeof(byte)); }
	void Read(float &d) { Read(&d, sizeof(float)); }
	void Read(double &d) { Read(&d, sizeof(double)); }
	void Read(bool &d) { Read(&d, sizeof(bool)); }
};

class CMachineDataOutput
{
public:
	virtual void Write(void *pbuf, int const numbytes);

	void Write(int d) { Write(&d, sizeof(int)); }
	void Write(dword d) { Write(&d, sizeof(dword)); }
	void Write(short d) { Write(&d, sizeof(short)); }
	void Write(word d) { Write(&d, sizeof(word)); }
	void Write(char d) { Write(&d, sizeof(char)); }
	void Write(byte d) { Write(&d, sizeof(byte)); }
	void Write(float d) { Write(&d, sizeof(float)); }
	void Write(double d) { Write(&d, sizeof(double)); }
	void Write(bool d) { Write(&d, sizeof(bool)); }
	void Write(char const *str) { Write((void *)str, (int)strlen(str)+1); }
};

#define EIF_SUSTAIN			1
#define EIF_LOOP			2

class CEnvelopeInfo
{
public:
	char const *Name;
	int Flags;
};

class CMachineInterface
{
public:
	virtual ~CMachineInterface() {}
	virtual void Init(CMachineDataInput * const pi) {}
	virtual void Tick() {}
	virtual bool Work(float *psamples, int numsamples, int const mode) { return false; }
	virtual bool WorkMonoToStereo(float *pin, float *pout, int numsamples, int const mode) { return false; }
	virtual void Stop() {}
	virtual void Save(CMachineDataOutput * const po) {}
	virtual void AttributesChanged() {}
	virtual void Command(int const i) {}
	virtual void SetNumTracks(int const n) {}
	virtual void MuteTrack(int const i) {}
	virtual bool IsTrackMuted(int const i) const { return false; }
	virtual void MidiNote(int const channel, int const value, int const velocity) {}
	virtual void Event(dword const data) {}
	virtual char const *DescribeValue(int const param, int const value) { return NULL; }
	virtual CEnvelopeInfo const **GetEnvelopeInfos() { return NULL; }
	virtual bool PlayWave(int const wave, int const note, float const volume) { return false; }
	virtual void StopWave() {}
	virtual int GetWaveEnvPlayPos(int const env) { return -1; }

public:
	void *GlobalVals;
	void *TrackVals;
	int *AttrVals;
	CMasterInfo *pMasterInfo;
	CMICallbacks *pCB;
};

class CMachineInterfaceEx
{
public:
	virtual char const *DescribeParam(int const param) { return NULL; }
	virtual bool SetInstrument(char const *name) { return false; }
	virtual void GetSubMenu(int const i, CMachineDataOutput *pout) {}
	virtual void AddInput(char const *macname, bool stereo) {}
	virtual void DeleteInput(char const *macename) {}
	virtual void RenameInput(char const *macoldname, char const *macnewname) {}
	virtual void Input(float *psamples, int numsamples, float amp) {}
	virtual void MidiControlChange(int const ctrl, int const channel, int const value) {}
	virtual void SetInputChannels(char const *macname, bool stereo) {}
	virtual bool HandleInput(int index, int amp, int pan) { return false; }
	virtual void CreatePattern(CPattern *p, int numrows) {}
	virtual void CreatePatternCopy(CPattern *pnew, CPattern const *pold) {}
	virtual void DeletePattern(CPattern *p) {}
	virtual void RenamePattern(CPattern *p, char const *name) {}
	virtual void SetPatternLength(CPattern *p, int length) {}
	virtual void PlayPattern(CPattern *p, CSequence *s, int offset) {}
	virtual void *CreatePatternEditor(void *parenthwnd) { return NULL; }
	virtual void SetEditorPattern(CPattern *p) {}
	virtual void AddTrack() {}
	virtual void DeleteLastTrack() {}
	virtual bool EnableCommandUI(int id) { return false; }
	virtual void DrawPatternBox(CDrawPatternBoxContext *ctx) {}
	virtual void SetPatternTargetMachine(CPattern *p, CMachine *pmac) {}
	virtual void *CreateEmbeddedGUI(void *parenthwnd) { return NULL; }
	virtual void SelectWave(int i) {}
	virtual void SetDeletedState(bool deleted) {}
	virtual bool ShowPatternProperties() { return false; }
	virtual bool ImportPattern(CPattern *p) { return false; }
	virtual int GetLatency() { return 0; }
	virtual void RecordControlChange(CMachine *pmac, int group, int track, int param, int value) {}
	virtual void GotMidiFocus() {}
	virtual void LostMidiFocus() {}
	virtual void BeginWriteToPlayingPattern(CMachine *pmac, int quantization, CPatternWriteInfo &outpwi) {}
	virtual void WriteToPlayingPattern(CMachine *pmac, int group, int track, int param, int value) {}
	virtual void EndWriteToPlayingPattern(CMachine *pmac) {}
	virtual bool ShowPatternEditorHelp() { return false; }
	virtual void SetBaseOctave(int bo) {}
	virtual int GetEditorPatternPosition() { return 0; }
	virtual void MultiWork(float const * const *inputs, float **outputs, int numsamples) {}
	virtual char const *GetChannelName(bool input, int index) { return NULL; }
	virtual bool HandleGUIMessage(CMachineDataOutput *pout, CMachineDataInput *pin) { return false; }
	virtual bool ExportMidiEvents(CPattern *p, CMachineDataOutput *pout) { return false; }
	virtual bool ImportMidiEvents(CPattern *p, CMachineDataInput *pin) { return false; }
	virtual void ThemeChanged() {}
	virtual void Load(CMachineDataInput * const pi) {}
	virtual void ImportFinished() {}
	virtual bool GetInstrument(char *buf, int bufsize) { return false; }
	virtual void UpdateWaveReferences(CPattern *ppat, byte const *remap) {}
	virtual bool IsValidAsciiChar(int param, char ch) { return true; }
	virtual void DebugConsoleMessage(char const *text) {}
	virtual void Dummy1() {} virtual void Dummy2() {} virtual void Dummy3() {} virtual void Dummy4() {}
	virtual void Dummy5() {} virtual void Dummy6() {} virtual void Dummy7() {} virtual void Dummy8() {}
	virtual void Dummy9() {} virtual void Dummy10() {} virtual void Dummy11() {} virtual void Dummy12() {}
	virtual void Dummy13() {} virtual void Dummy14() {} virtual void Dummy15() {} virtual void Dummy16() {}
	virtual void Dummy17() {} virtual void Dummy18() {} virtual void Dummy19() {} virtual void Dummy20() {}
	virtual void Dummy21() {} virtual void Dummy22() {} virtual void Dummy23() {} virtual void Dummy24() {}
	virtual void Dummy25() {} virtual void Dummy26() {} virtual void Dummy27() {} virtual void Dummy28() {}
	virtual void Dummy29() {} virtual void Dummy30() {} virtual void Dummy31() {} virtual void Dummy32() {}
};

#endif
