#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "../buzz/BuzzMachineLoader.h"
#include "../buzz/BuzzParamLayout.h"
#include "../common/SEHGuard.h"
#include "../resample/BuzzResampler.h"
#include "plugids.h"

#ifdef BUZZVST_64BIT
#include "../bridge/BridgeClient.h"
#endif

#include <vector>
#include <string>
#include <atomic>
#include <mutex>

namespace BuzzVst {

using namespace Steinberg;
using namespace Steinberg::Vst;

// Base processor class shared between Generator and Effect variants.
// In 32-bit mode: loads Buzz machines directly via BuzzMachineLoader.
// In 64-bit mode: delegates to a 32-bit bridge host process via BridgeClient.
class BuzzProcessor : public AudioEffect {
public:
	BuzzProcessor();
	~BuzzProcessor() override;

	// AudioEffect overrides
	tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
	tresult PLUGIN_API terminate() SMTG_OVERRIDE;
	tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE;
	tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;
	tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API setupProcessing(ProcessSetup& newSetup) SMTG_OVERRIDE;
	tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE;
	tresult PLUGIN_API notify(IMessage* message) SMTG_OVERRIDE;

protected:
	// Override in subclass to set up audio buses
	virtual void setupBuses() = 0;

	// Override in subclass to process audio through the Buzz machine
	virtual void processAudioBlock(float** inputs, float** outputs,
	                                int32 numInputChannels, int32 numOutputChannels,
	                                int32 numSamples) = 0;

	// Check if this processor accepts the given machine type
	virtual bool acceptsMachineType(int type) const = 0;

	// Process parameter changes from the host
	void processParameterChanges(IParameterChanges* changes);

	// Process MIDI events (notes, CC, pressure)
	void processMidiEvents(IEventList* events);

	// Write a Buzz note value into the first pt_note parameter found.
	// If velocity >= 0, also write it to the volume/velocity param following the note.
	void writeNoteToParams(int buzzNote, int velocity = -1);

	// Update master info from process context
	void updateMasterInfo(ProcessContext* ctx);

	// Load or reload the Buzz machine DLL
	bool loadBuzzMachine(const std::string& path);

	// Send wave slot names to the controller for GUI display
	void sendWaveSlotsToController();

	// Notify controller that a machine was successfully loaded
	void sendMachineLoadedToController();

	// 32-bit direct loader (always present for compilation, but only
	// actively used in 32-bit builds)
	BuzzMachineLoader loader;

#ifdef BUZZVST_64BIT
	BridgeClient bridge;
	// The bridge pipe is not thread-safe. The audio thread (process())
	// and the message thread (notify(), sendMachineLoadedToController,
	// setState, etc.) can both issue bridge commands. Without a lock,
	// two writers interleave bytes on the pipe and the bridge sees a
	// corrupted protocol — after which every subsequent Work() fails.
	// This manifested as "loading Rout 909 crashes BespokeSynth": the
	// controller's load-complete poll triggered hundreds of bridge.
	// DescribeValue() calls on the message thread while the audio
	// thread was still calling bridge.Work() every buffer.
	std::recursive_mutex bridgeMutex;
	bool bridgeStarted = false;
	std::string bridgeHostPath;
	std::vector<BridgeParamInfo> bridgeParamInfos; // cached for sending to controller
	std::string bridgeMachineName; // cached machine name from bridge
	// Value descriptions cached at load time so sendMachineLoadedToController
	// doesn't have to hammer the bridge pipe from the message thread while
	// the audio thread is using it. Outer vector is indexed by flat param
	// index (globals first, then track params), inner vector is indexed by
	// value - minValue. Empty inner vector means "no descriptions for this
	// param" (either out of range or all blank).
	std::vector<std::vector<std::string>> bridgeValueDescriptions;
	// Cached note/velocity slot indices for writeNoteToParams (since loader is unavailable)
	int bridgeGlobalNoteSlot = -1;
	int bridgeGlobalVelSlot = -1;
	int bridgeGlobalTrigSlot = -1; // non-state byte trigger in globals (e.g. ld clap Velocity)
	int bridgeTrackNoteSlot = -1;
	int bridgeTrackVelSlot = -1;
	int bridgeTrackTrigSlot = -1; // pt_switch trigger (for machines without pt_note)
	int bridgeNumGlobalParams = 0;
	bool ensureBridgeRunning();
#endif

	std::string dllPath;
	bool machineReady = false;

	// Wave paths loaded via bridge (for state persistence in 64-bit mode)
	struct WavePathEntry { int slotIndex; std::string filePath; };
	std::vector<WavePathEntry> loadedWavePaths;

	// Deferred machine load: notify() sets the path, process() does the actual load.
	// This avoids race conditions between the UI thread (notify) and audio thread (process).
	std::string pendingDllPath;
	bool hasPendingLoad = false;
	bool pendingMachineLoaded = false;  // set after successful deferred load
	bool pendingLoadFailed = false;     // set after failed deferred load

	// Machine info (populated from loader in 32-bit, or bridge in 64-bit)
	int machineType = 0;
	int machineFlags = 0;

	// Current parameter values (Buzz integer values)
	std::vector<int> currentGlobalValues;
	// Track parameters: outer = track index, inner = param index within track
	std::vector<std::vector<int>> currentTrackValues;

	// Flags for which params changed since last tick
	std::vector<bool> globalParamChanged;
	std::vector<std::vector<bool>> trackParamChanged;

	// Track management
	int numTracks = 1;
	int machineMinTracks = 0;
	int machineMaxTracks = 0;
	int numTrackParams = 0; // params per track

	// Deferred note-off: if a note-off arrives before a pending note-on is ticked,
	// defer it so the machine sees the note-on first.
	bool pendingNoteOff = false;

	// Cached master info to avoid sending SetMasterInfo on every process() call
	int lastSentBpm = 0;
	int lastSentSampleRate = 0;
	int lastSentTpb = 0;

	// Tick timing
	int samplesUntilNextTick = 0;
	bool firstTick = true;

	// After setState restores params, ignore host parameter pushes until
	// the first tick sends the correct values to the machine.  Some hosts
	// push stale defaults from the controller, overwriting saved values.
	bool stateJustRestored = false;

	// Bypass
	bool bBypass = false;

	// Work buffers for Buzz's 256-sample limit. Oversized to 2x + slop
	// because some machines (e.g. Rout 909 via DSP_Add/DSP_Zero from
	// dsplib.dll; Ruff SPECCY II and other MDK-derived generators)
	// interpret psamples as stereo-interleaved and write up to
	// 2 * numsamples floats into it, or simply write past the nominal
	// numsamples end. Without the extra room a single Work call can
	// overflow this class member into adjacent members / vtable and
	// either crash (debug heap / /GS) or silently corrupt. This was
	// the cause of the "loading Rout 909 crashes BespokeSynth" reports.
	float workBufLeft[MAX_BUFFER_LENGTH * 2 + 16];
	float workBufRight[MAX_BUFFER_LENGTH * 2 + 16];

	// ---- Sample-rate conversion (Buzz machines assume 44100 Hz) ----
	bool needsResampling = false;  // true when host rate != 44100

	// Whether to actually resample.  Both generators and effects run at 44100
	// internally — most Buzz machines have hardcoded DSP for that rate.
	bool shouldResample() const { return needsResampling; }
	double resampleFracAccum = 0.0;  // fractional sample accumulator

	// Resamplers (per-channel): output upsamplers + input downsamplers (effects)
	BuzzResampler resamplerOutL, resamplerOutR;
	BuzzResampler resamplerInL, resamplerInR;

	// Intermediate buffers for machine-rate audio (allocated when resampling)
	std::vector<float> resampleBufOutL, resampleBufOutR;
	std::vector<float> resampleBufInL, resampleBufInR;

	// Compute how many machine-rate samples to generate for a given host block
	int computeMachineSamples(int hostSamples);

	// Initialise or tear down resamplers based on current host sample rate
	void updateResamplers();
};

} // namespace BuzzVst
