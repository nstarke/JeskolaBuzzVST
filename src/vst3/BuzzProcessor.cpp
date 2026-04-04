#include "BuzzProcessor.h"
#include "ParameterMapping.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <algorithm>

namespace BuzzVst {

BuzzProcessor::BuzzProcessor()
{
	memset(workBufLeft, 0, sizeof(workBufLeft));
	memset(workBufRight, 0, sizeof(workBufRight));
}

BuzzProcessor::~BuzzProcessor()
{
	loader.Unload();
}

tresult PLUGIN_API BuzzProcessor::initialize(FUnknown* context)
{
	tresult result = AudioEffect::initialize(context);
	if (result != kResultOk)
		return result;

	// Let subclass set up audio buses
	setupBuses();

	// Add event input for MIDI
	addEventInput(STR16("Event In"), 1);

	return kResultOk;
}

tresult PLUGIN_API BuzzProcessor::terminate()
{
#ifdef BUZZVST_64BIT
	bridge.Stop();
#else
	loader.Unload();
#endif
	machineReady = false;
	return AudioEffect::terminate();
}

tresult PLUGIN_API BuzzProcessor::setActive(TBool state)
{
	if (state) {
		if (!dllPath.empty() && !machineReady) {
			loadBuzzMachine(dllPath);
		}
		firstTick = true;
		samplesUntilNextTick = 0;
	} else {
		if (machineReady) {
#ifdef BUZZVST_64BIT
			bridge.StopMachine();
#else
			loader.StopMachine();
#endif
		}
	}

	return AudioEffect::setActive(state);
}

tresult PLUGIN_API BuzzProcessor::setupProcessing(ProcessSetup& newSetup)
{
	// Update master info with the new sample rate
	if (machineReady) {
#ifdef BUZZVST_64BIT
		bridge.SetMasterInfo(125, (int)newSetup.sampleRate, 4);
#else
		loader.UpdateMasterInfo(125.0, newSetup.sampleRate);
#endif
	}
	return AudioEffect::setupProcessing(newSetup);
}

tresult PLUGIN_API BuzzProcessor::canProcessSampleSize(int32 symbolicSampleSize)
{
	// We only support 32-bit float (Buzz machines use floats)
	if (symbolicSampleSize == kSample32)
		return kResultTrue;
	return kResultFalse;
}

tresult PLUGIN_API BuzzProcessor::process(ProcessData& data)
{
	// Process parameter changes
	if (data.inputParameterChanges) {
		processParameterChanges(data.inputParameterChanges);
	}

	// Sanity check numSamples
	if (data.numSamples <= 0 || data.numSamples > 65536)
		return kResultOk;

	// Process MIDI events
	if (data.inputEvents) {
		processMidiEvents(data.inputEvents);
	}

	// Update timing from transport
	if (data.processContext) {
		updateMasterInfo(data.processContext);
	}

	if (!machineReady || loader.IsFaulted()) {
		static int silenceLogCount = 0;
		if (silenceLogCount < 3) {
			char dbg[128];
			snprintf(dbg, sizeof(dbg), "[BuzzBridge] process: silence (machineReady=%d faulted=%d)\n",
				(int)machineReady, (int)loader.IsFaulted());
			OutputDebugStringA(dbg);
			silenceLogCount++;
		}
		// No machine loaded - output silence
		if (data.numOutputs > 0) {
			for (int32 ch = 0; ch < data.outputs[0].numChannels; ch++) {
				memset(data.outputs[0].channelBuffers32[ch], 0,
				       data.numSamples * sizeof(float));
			}
			data.outputs[0].silenceFlags = (1ULL << data.outputs[0].numChannels) - 1;
		}
		return kResultOk;
	}

	// Get output buffer info
	if (data.numOutputs == 0)
		return kResultOk;

	float** outputs = data.outputs[0].channelBuffers32;
	int32 numOutputChannels = data.outputs[0].numChannels;

	float** inputs = nullptr;
	int32 numInputChannels = 0;
	if (data.numInputs > 0) {
		inputs = data.inputs[0].channelBuffers32;
		numInputChannels = data.inputs[0].numChannels;
	}

	// Process in chunks, handling tick boundaries and Buzz's 256-sample limit
	int32 samplesRemaining = data.numSamples;
	int32 offset = 0;

	// Compute samples per tick
	int32 samplesPerTick;
#ifdef BUZZVST_64BIT
	{
		double bpm = 125.0;
		if (data.processContext && (data.processContext->state & ProcessContext::kTempoValid))
			bpm = data.processContext->tempo;
		if (bpm < 16) bpm = 125;
		int tpb = 4;
		samplesPerTick = (int32)((60.0 * processSetup.sampleRate) / (bpm * tpb));
		if (samplesPerTick < 1) samplesPerTick = 1;
	}
#else
	samplesPerTick = loader.GetMasterInfo()->SamplesPerTick;
#endif

	while (samplesRemaining > 0) {
		// Fire a tick if needed
		if (samplesUntilNextTick <= 0 || firstTick) {

#ifdef BUZZVST_64BIT
			// Build tick params for bridge
			std::vector<BridgeTickParam> tickParams;
			for (int i = 0; i < (int)currentGlobalValues.size(); i++) {
				if (i < (int)globalParamChanged.size() && globalParamChanged[i]) {
					BridgeTickParam tp = {i, currentGlobalValues[i]};
					tickParams.push_back(tp);
					globalParamChanged[i] = false;
				} else if (firstTick) {
					BridgeTickParam tp = {i, currentGlobalValues[i]};
					tickParams.push_back(tp);
				}
			}
			for (int t = 0; t < numTracks && t < (int)currentTrackValues.size(); t++) {
				auto& tv = currentTrackValues[t];
				auto& tp = trackParamChanged[t];
				for (int i = 0; i < (int)tv.size(); i++) {
					if (i < (int)tp.size() && tp[i]) {
						BridgeTickParam btp = {1000 + t * (int)numTrackParams + i, tv[i]};
						tickParams.push_back(btp);
						tp[i] = false;
					} else if (firstTick) {
						BridgeTickParam btp = {1000 + t * (int)numTrackParams + i, tv[i]};
						tickParams.push_back(btp);
					}
				}
			}
			BridgeTickParam sentinel = {-1, 0};
			tickParams.push_back(sentinel);
			bridge.Tick(tickParams);
#else
			// Direct 32-bit tick
			auto* machine = loader.GetMachine();
			auto* layout = loader.GetParamLayout();
			auto* info = loader.GetInfo();

			if (machine && machine->GlobalVals && layout) {
				layout->WriteAllNoValues(machine->GlobalVals);

				auto& globalSlots = layout->GetGlobalSlots();
				for (int i = 0; i < (int)globalSlots.size(); i++) {
					if (i < (int)globalParamChanged.size() && globalParamChanged[i]) {
						layout->WriteGlobalParam(machine->GlobalVals, i, currentGlobalValues[i]);
						globalParamChanged[i] = false;
					} else if (firstTick && (globalSlots[i].param->Flags & MPF_STATE)) {
						layout->WriteGlobalParam(machine->GlobalVals, i, currentGlobalValues[i]);
					}
				}

				if (machine->TrackVals && info->numTrackParameters > 0 && numTracks > 0) {
					layout->WriteTrackAllNoValues(machine->TrackVals, numTracks);

					auto& trackSlots = layout->GetTrackSlots();
					for (int t = 0; t < numTracks && t < (int)currentTrackValues.size(); t++) {
						auto& tv = currentTrackValues[t];
						auto& tp = trackParamChanged[t];
						for (int i = 0; i < (int)trackSlots.size() && i < (int)tv.size(); i++) {
							if (tp[i]) {
								layout->WriteTrackParam(machine->TrackVals, t, i, tv[i]);
								tp[i] = false;
							} else if (firstTick && (trackSlots[i].param->Flags & MPF_STATE)) {
								layout->WriteTrackParam(machine->TrackVals, t, i, tv[i]);
							}
						}
					}
				}
			}

			if (machine) {
				SEH_Call([&]() { machine->Tick(); });
			}

			loader.GetMasterInfo()->PosInTick = 0;
#endif
			samplesUntilNextTick = samplesPerTick;
			firstTick = false;

			// Apply deferred note-off (from a short gate where note-off arrived
			// before the note-on was ticked)
			if (pendingNoteOff) {
				pendingNoteOff = false;
				writeNoteToParams(NOTE_OFF);
			}
		}

		int32 blockSize = std::min(samplesRemaining, samplesUntilNextTick);
		blockSize = std::min(blockSize, (int32)MAX_BUFFER_LENGTH);

		// Set up offset pointers for this chunk
		float* chunkInputs[2] = { nullptr, nullptr };
		float* chunkOutputs[2] = { nullptr, nullptr };

		if (inputs) {
			for (int32 ch = 0; ch < std::min(numInputChannels, (int32)2); ch++) {
				chunkInputs[ch] = inputs[ch] + offset;
			}
		}
		for (int32 ch = 0; ch < std::min(numOutputChannels, (int32)2); ch++) {
			chunkOutputs[ch] = outputs[ch] + offset;
		}

		// Let subclass handle the actual audio processing
		processAudioBlock(chunkInputs, chunkOutputs,
		                  numInputChannels, numOutputChannels, blockSize);

		offset += blockSize;
		samplesRemaining -= blockSize;
		samplesUntilNextTick -= blockSize;
#ifndef BUZZVST_64BIT
		loader.GetMasterInfo()->PosInTick += blockSize;
#endif
	}

	data.outputs[0].silenceFlags = 0;
	return kResultOk;
}

void BuzzProcessor::processParameterChanges(IParameterChanges* changes)
{
	int32 numParamsChanged = changes->getParameterCount();

	for (int32 i = 0; i < numParamsChanged; i++) {
		IParamValueQueue* paramQueue = changes->getParameterData(i);
		if (!paramQueue) continue;

		ParamValue value;
		int32 sampleOffset;
		int32 numPoints = paramQueue->getPointCount();

		// Use the last value in the queue
		if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) != kResultTrue)
			continue;

		ParamID id = paramQueue->getParameterId();

		if (id == kBypassParamID) {
			bBypass = (value > 0.5);
			continue;
		}

		if (!machineReady) continue;

		int numGlobal = (int)currentGlobalValues.size();

		// Check if it's a global parameter
		if (id >= kBuzzGlobalParamBase && id < kBuzzGlobalParamBase + numGlobal) {
			int slotIdx = id - kBuzzGlobalParamBase;
#ifdef BUZZVST_64BIT
			if (slotIdx < (int)bridgeParamInfos.size()) {
				auto& pi = bridgeParamInfos[slotIdx];
				int range = pi.maxValue - pi.minValue;
				int buzzVal = (range > 0) ? pi.minValue + (int)(value * range + 0.5) : pi.minValue;
				if (buzzVal < pi.minValue) buzzVal = pi.minValue;
				if (buzzVal > pi.maxValue) buzzVal = pi.maxValue;
				currentGlobalValues[slotIdx] = buzzVal;
				globalParamChanged[slotIdx] = true;
			}
#else
			auto* layout = loader.GetParamLayout();
			auto& globalSlots = layout->GetGlobalSlots();
			if (slotIdx < (int)globalSlots.size()) {
				int buzzVal = ParameterMapping::NormalizedToBuzz(value, globalSlots[slotIdx].param);
				currentGlobalValues[slotIdx] = buzzVal;
				globalParamChanged[slotIdx] = true;
			}
#endif
		}
		// Check if it's a track parameter (decode track index from ID)
		else if (id >= kBuzzTrackParamBase) {
			int trackOffset = id - kBuzzTrackParamBase;
			int trackIdx = trackOffset / kTrackParamStride;
			int slotIdx = trackOffset % kTrackParamStride;
			if (trackIdx >= 0 && trackIdx < numTracks &&
			    slotIdx >= 0 && slotIdx < numTrackParams) {
#ifdef BUZZVST_64BIT
				int piIdx = numGlobal + slotIdx;
				if (piIdx < (int)bridgeParamInfos.size()) {
					auto& pi = bridgeParamInfos[piIdx];
					int range = pi.maxValue - pi.minValue;
					int buzzVal = (range > 0) ? pi.minValue + (int)(value * range + 0.5) : pi.minValue;
					if (buzzVal < pi.minValue) buzzVal = pi.minValue;
					if (buzzVal > pi.maxValue) buzzVal = pi.maxValue;
					currentTrackValues[trackIdx][slotIdx] = buzzVal;
					trackParamChanged[trackIdx][slotIdx] = true;
				}
#else
				auto* layout = loader.GetParamLayout();
				auto& trackSlots = layout->GetTrackSlots();
				if (slotIdx < (int)trackSlots.size()) {
					int buzzVal = ParameterMapping::NormalizedToBuzz(value, trackSlots[slotIdx].param);
					currentTrackValues[trackIdx][slotIdx] = buzzVal;
					trackParamChanged[trackIdx][slotIdx] = true;
				}
#endif
			}
		}
	}
}

// Convert MIDI note number (0-127) to Buzz note format.
// Buzz: (octave << 4) | note, where note = 1-12, octave = 0-9.
static int MidiNoteToBuzz(int midiNote)
{
	if (midiNote < 0) midiNote = 0;
	if (midiNote > 127) midiNote = 127;
	int octave = midiNote / 12;
	int note = (midiNote % 12) + 1;
	if (octave > 9) octave = 9;
	return (octave << 4) | note;
}

// Check if a parameter name suggests it's a volume/velocity control.
static bool IsVelocityParam(const CMachineParameter* p)
{
	if (!p || !p->Name) return false;
	if (p->Type != pt_byte) return false;

	// Check common Buzz naming conventions
	const char* name = p->Name;
	for (const char* keyword : {"olum", "eloc", "Amp", "amp", "vel", "Vol", "vol", "Vel"}) {
		if (strstr(name, keyword)) return true;
	}
	return false;
}

// Find the velocity/volume parameter index associated with a note parameter.
// Returns -1 if not found.  Strategy:
//   1. The byte param immediately after the note (standard Buzz convention: Note, Volume)
//   2. Fallback: any byte param whose name suggests volume/velocity
static int FindVelocitySlot(const std::vector<ParamSlot>& slots, int noteSlotIndex)
{
	// Primary: adjacent byte param (standard Buzz track layout is Note then Volume)
	int next = noteSlotIndex + 1;
	if (next < (int)slots.size() && slots[next].param->Type == pt_byte) {
		return next;
	}

	// Fallback: scan for a named velocity/volume param
	for (int j = 0; j < (int)slots.size(); j++) {
		if (j == noteSlotIndex) continue;
		if (IsVelocityParam(slots[j].param)) return j;
	}

	return -1;
}

// Write a Buzz note value into the first pt_note parameter found in global and track slots.
// If velocity >= 0, also write it to the volume/velocity param following the note.
void BuzzProcessor::writeNoteToParams(int buzzNote, int velocity)
{
#ifdef BUZZVST_64BIT
	// 64-bit mode: use cached slot indices from bridgeParamInfos
	if (bridgeGlobalNoteSlot >= 0 && bridgeGlobalNoteSlot < (int)currentGlobalValues.size()) {
		currentGlobalValues[bridgeGlobalNoteSlot] = buzzNote;
		globalParamChanged[bridgeGlobalNoteSlot] = true;

		if (velocity >= 0 && buzzNote != NOTE_OFF && bridgeGlobalVelSlot >= 0 &&
		    bridgeGlobalVelSlot < (int)currentGlobalValues.size()) {
			auto& vp = bridgeParamInfos[bridgeGlobalVelSlot];
			int buzzVel = vp.minValue +
				(int)((float)velocity / 127.0f * (float)(vp.maxValue - vp.minValue) + 0.5f);
			if (buzzVel < vp.minValue) buzzVel = vp.minValue;
			if (buzzVel > vp.maxValue) buzzVel = vp.maxValue;
			currentGlobalValues[bridgeGlobalVelSlot] = buzzVel;
			globalParamChanged[bridgeGlobalVelSlot] = true;
		}
	}

	if (bridgeTrackNoteSlot >= 0 && numTracks > 0 && !currentTrackValues.empty()) {
		auto& tv = currentTrackValues[0];
		auto& tp = trackParamChanged[0];
		if (bridgeTrackNoteSlot < (int)tv.size()) {
			tv[bridgeTrackNoteSlot] = buzzNote;
			tp[bridgeTrackNoteSlot] = true;

			if (velocity >= 0 && buzzNote != NOTE_OFF && bridgeTrackVelSlot >= 0 &&
			    bridgeTrackVelSlot < (int)tv.size()) {
				auto& vp = bridgeParamInfos[bridgeNumGlobalParams + bridgeTrackVelSlot];
				int buzzVel = vp.minValue +
					(int)((float)velocity / 127.0f * (float)(vp.maxValue - vp.minValue) + 0.5f);
				if (buzzVel < vp.minValue) buzzVel = vp.minValue;
				if (buzzVel > vp.maxValue) buzzVel = vp.maxValue;
				tv[bridgeTrackVelSlot] = buzzVel;
				tp[bridgeTrackVelSlot] = true;
			}
		}
	}

	{
		static int noteLogCount = 0;
		if (noteLogCount++ < 5) {
			char dbg[256];
			snprintf(dbg, sizeof(dbg), "[BuzzBridge] writeNoteToParams: buzzNote=%d vel=%d gSlot=%d tSlot=%d\n",
				buzzNote, velocity, bridgeGlobalNoteSlot, bridgeTrackNoteSlot);
			OutputDebugStringA(dbg);
		}
	}
#else
	auto* layout = loader.GetParamLayout();

	// Global params
	auto& gSlots = layout->GetGlobalSlots();
	for (int j = 0; j < (int)gSlots.size(); j++) {
		if (gSlots[j].param->Type == pt_note) {
			currentGlobalValues[j] = buzzNote;
			globalParamChanged[j] = true;

			// Route velocity to the associated volume param
			if (velocity >= 0 && buzzNote != NOTE_OFF) {
				int velSlot = FindVelocitySlot(gSlots, j);
				if (velSlot >= 0) {
					// Scale MIDI velocity (0-127) to the param's range
					const CMachineParameter* vp = gSlots[velSlot].param;
					int buzzVel = vp->MinValue +
						(int)((float)velocity / 127.0f * (float)(vp->MaxValue - vp->MinValue) + 0.5f);
					if (buzzVel < vp->MinValue) buzzVel = vp->MinValue;
					if (buzzVel > vp->MaxValue) buzzVel = vp->MaxValue;
					currentGlobalValues[velSlot] = buzzVel;
					globalParamChanged[velSlot] = true;
				}
			}
			break;
		}
	}

	// Track params — write to track 0 (MIDI note routing to other tracks is future work)
	auto& tSlots = layout->GetTrackSlots();
	if (numTracks > 0 && !currentTrackValues.empty()) {
		auto& tv = currentTrackValues[0];
		auto& tp = trackParamChanged[0];
		for (int j = 0; j < (int)tSlots.size() && j < (int)tv.size(); j++) {
			if (tSlots[j].param->Type == pt_note) {
				tv[j] = buzzNote;
				tp[j] = true;

				if (velocity >= 0 && buzzNote != NOTE_OFF) {
					int velSlot = FindVelocitySlot(tSlots, j);
					if (velSlot >= 0 && velSlot < (int)tv.size()) {
						const CMachineParameter* vp = tSlots[velSlot].param;
						int buzzVel = vp->MinValue +
							(int)((float)velocity / 127.0f * (float)(vp->MaxValue - vp->MinValue) + 0.5f);
						if (buzzVel < vp->MinValue) buzzVel = vp->MinValue;
						if (buzzVel > vp->MaxValue) buzzVel = vp->MaxValue;
						tv[velSlot] = buzzVel;
						tp[velSlot] = true;
					}
				}
				break;
			}
		}
	}
#endif
}

void BuzzProcessor::processMidiEvents(IEventList* events)
{
	if (!machineReady || !events) return;

	int32 numEvents = events->getEventCount();
	for (int32 i = 0; i < numEvents; i++) {
		Event event {};
		if (events->getEvent(i, event) != kResultOk) continue;

		switch (event.type) {
			case Event::kNoteOnEvent: {
				int midiNote = event.noteOn.pitch;
				int velocity = (int)(event.noteOn.velocity * 127.0f + 0.5f);
				if (velocity < 0) velocity = 0;
				if (velocity > 127) velocity = 127;

				pendingNoteOff = false; // cancel any deferred note-off
				writeNoteToParams(MidiNoteToBuzz(midiNote), velocity);

#ifdef BUZZVST_64BIT
				bridge.SendMidiNote(event.noteOn.channel, midiNote, velocity);
#else
				if (auto* machine = loader.GetMachine()) {
					SEH_Call([&]() { machine->MidiNote(event.noteOn.channel, midiNote, velocity); });
				}
#endif
				break;
			}

			case Event::kNoteOffEvent: {
				// If there's a pending note-on that hasn't been ticked yet, defer the
				// note-off so the machine sees the note-on first on the next tick.
				bool noteStillPending = false;
#ifdef BUZZVST_64BIT
				if (bridgeTrackNoteSlot >= 0 && !currentTrackValues.empty()) {
					auto& tp = trackParamChanged[0];
					if (bridgeTrackNoteSlot < (int)tp.size() && tp[bridgeTrackNoteSlot]) {
						int val = currentTrackValues[0][bridgeTrackNoteSlot];
						if (val != NOTE_OFF && val != NOTE_NO)
							noteStillPending = true;
					}
				}
#else
				{
					auto* layout = loader.GetParamLayout();
					auto& tSlots = layout->GetTrackSlots();
					for (int j = 0; j < (int)tSlots.size(); j++) {
						if (tSlots[j].param->Type == pt_note && !currentTrackValues.empty()) {
							auto& tp = trackParamChanged[0];
							if (j < (int)tp.size() && tp[j]) {
								int val = currentTrackValues[0][j];
								if (val != NOTE_OFF && val != NOTE_NO)
									noteStillPending = true;
							}
							break;
						}
					}
				}
#endif
				if (noteStillPending) {
					pendingNoteOff = true;
				} else {
					writeNoteToParams(NOTE_OFF);
#ifdef BUZZVST_64BIT
					bridge.SendMidiNote(event.noteOff.channel, event.noteOff.pitch, 0);
#else
					if (auto* machine = loader.GetMachine()) {
						SEH_Call([&]() { machine->MidiNote(event.noteOff.channel, event.noteOff.pitch, 0); });
					}
#endif
				}
				break;
			}

			case Event::kPolyPressureEvent: {
				int pressure = (int)(event.polyPressure.pressure * 127.0f + 0.5f);
				if (pressure < 0) pressure = 0;
				if (pressure > 127) pressure = 127;
#ifdef BUZZVST_64BIT
				bridge.SendMidiCC(kAfterTouch, event.polyPressure.channel, pressure);
#else
				if (auto* machineEx = loader.GetMachineEx()) {
					SEH_Call([&]() {
						machineEx->MidiControlChange(kAfterTouch, event.polyPressure.channel, pressure);
					});
				}
#endif
				break;
			}

			case Event::kLegacyMIDICCOutEvent: {
				int ctrl = event.midiCCOut.controlNumber;
				int channel = event.midiCCOut.channel;
				int value = event.midiCCOut.value;
#ifdef BUZZVST_64BIT
				bridge.SendMidiCC(ctrl, channel, value);
#else
				if (auto* machineEx = loader.GetMachineEx()) {
					if (ctrl < 128 || ctrl == kPitchBend || ctrl == kAfterTouch) {
						SEH_Call([&]() { machineEx->MidiControlChange(ctrl, channel, value); });
					}
				}
#endif
				break;
			}

			default:
				break;
		}
	}
}

void BuzzProcessor::updateMasterInfo(ProcessContext* ctx)
{
	if (!ctx || !machineReady) return;

	double bpm = 125.0;
	if (ctx->state & ProcessContext::kTempoValid) {
		bpm = ctx->tempo;
	}

	// Buzz ticks-per-beat is a pattern resolution concept (typically 4),
	// not a time signature value. VST3 doesn't have a direct equivalent.
	// We use a fixed default of 4, which matches what most Buzz machines expect.
	// The time signature denominator (e.g. 4 in 4/4) is unrelated.
	int tpb = 4;

#ifdef BUZZVST_64BIT
	bridge.SetMasterInfo((int)bpm, (int)processSetup.sampleRate, tpb);
#else
	loader.UpdateMasterInfo(bpm, processSetup.sampleRate, tpb);
#endif
}

#ifdef BUZZVST_64BIT

bool BuzzProcessor::ensureBridgeRunning()
{
	if (bridge.IsRunning()) return true;

	// Find the bridge host exe next to the VST3 plugin DLL.
	if (bridgeHostPath.empty()) {
		char modulePath[MAX_PATH] = {};

		// Locate our own DLL module. We use GetModuleHandleExA with a
		// pointer to a global variable defined in this translation unit.
		// This is guaranteed to be in our DLL's data section.
		static volatile int sBridgeModuleMarker = 42;
		HMODULE hMod = nullptr;
		GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)&sBridgeModuleMarker, &hMod);
		if (hMod) {
			GetModuleFileNameA(hMod, modulePath, MAX_PATH);
		}
		std::string dir = modulePath;
		auto pos = dir.find_last_of("\\/");
		if (pos != std::string::npos) dir = dir.substr(0, pos);
		bridgeHostPath = dir + "\\BuzzBridgeHost32.exe";

		char msg[512];
		snprintf(msg, sizeof(msg), "[BuzzBridge64] Plugin module: %s\n", modulePath);
		OutputDebugStringA(msg);
		snprintf(msg, sizeof(msg), "[BuzzBridge64] Looking for bridge host: %s\n", bridgeHostPath.c_str());
		OutputDebugStringA(msg);

		DWORD attrs = GetFileAttributesA(bridgeHostPath.c_str());
		if (attrs == INVALID_FILE_ATTRIBUTES) {
			OutputDebugStringA("[BuzzBridge64] ERROR: BuzzBridgeHost32.exe NOT FOUND at that path!\n");
		} else {
			OutputDebugStringA("[BuzzBridge64] BuzzBridgeHost32.exe found OK\n");
		}
	}

	return bridge.Start(bridgeHostPath);
}

bool BuzzProcessor::loadBuzzMachine(const std::string& path)
{
	machineReady = false;
	if (path.empty()) return false;

	if (!ensureBridgeRunning()) return false;

	bridge.Unload();

	if (!bridge.LoadDll(path)) return false;

	bridge.SetMasterInfo(125, (int)processSetup.sampleRate, 4);

	if (!bridge.InitMachine()) {
		bridge.Unload();
		return false;
	}

	// Get machine info from the bridge
	BridgeMachineInfo bmi = {};
	std::vector<BridgeParamInfo> paramInfos;
	if (!bridge.GetMachineInfo(bmi, paramInfos)) {
		bridge.Unload();
		return false;
	}
	bridgeParamInfos = paramInfos; // cache for sending to controller
	bridgeMachineName = bmi.name;

	if (!acceptsMachineType(bmi.type)) {
		char dbg[128];
		snprintf(dbg, sizeof(dbg), "[BuzzBridge] loadBuzzMachine: machine type %d not accepted\n", bmi.type);
		OutputDebugStringA(dbg);

		// Send typed error to controller
		if (auto msg = owned(allocateMessage())) {
			msg->setMessageID("BuzzMachineLoadFailed");
			msg->getAttributes()->setBinary("Path", path.c_str(), (Steinberg::uint32)path.size());
			const char* reason = (bmi.type == MT_EFFECT)
				? "This is an Effect machine. Load it in the BuzzBridge Effect plugin."
				: "This is a Generator machine. Load it in the BuzzBridge Generator plugin.";
			msg->getAttributes()->setBinary("Reason", reason, (Steinberg::uint32)strlen(reason));
			sendMessage(msg);
		}

		bridge.Unload();
		return false;
	}

	machineType = bmi.type;
	machineFlags = bmi.flags;

	// Initialize parameter value arrays from bridge info
	int numGlobal = bmi.numGlobalParams;
	numTrackParams = bmi.numTrackParams;
	machineMinTracks = bmi.minTracks;
	machineMaxTracks = bmi.maxTracks;
	numTracks = std::max(1, machineMinTracks);

	currentGlobalValues.resize(numGlobal);
	globalParamChanged.resize(numGlobal, true);

	for (int i = 0; i < numGlobal; i++) {
		auto& pi = paramInfos[i];
		if (pi.flags & MPF_STATE) {
			currentGlobalValues[i] = pi.defValue;
		} else {
			currentGlobalValues[i] = pi.noValue;
		}
	}

	currentTrackValues.resize(numTracks);
	trackParamChanged.resize(numTracks);
	for (int t = 0; t < numTracks; t++) {
		currentTrackValues[t].resize(numTrackParams);
		trackParamChanged[t].resize(numTrackParams, true);
		for (int i = 0; i < numTrackParams; i++) {
			auto& pi = paramInfos[numGlobal + i];
			if (pi.flags & MPF_STATE) {
				currentTrackValues[t][i] = pi.defValue;
			} else {
				currentTrackValues[t][i] = pi.noValue;
			}
		}
	}

	// Cache note/velocity slot indices for writeNoteToParams
	bridgeNumGlobalParams = numGlobal;
	bridgeGlobalNoteSlot = -1;
	bridgeGlobalVelSlot = -1;
	bridgeTrackNoteSlot = -1;
	bridgeTrackVelSlot = -1;

	for (int i = 0; i < numGlobal; i++) {
		if (paramInfos[i].type == pt_note) {
			bridgeGlobalNoteSlot = i;
			// Look for adjacent velocity param
			if (i + 1 < numGlobal && paramInfos[i + 1].type == pt_byte) {
				bridgeGlobalVelSlot = i + 1;
			}
			break;
		}
	}
	for (int i = 0; i < numTrackParams; i++) {
		if (paramInfos[numGlobal + i].type == pt_note) {
			bridgeTrackNoteSlot = i;
			if (i + 1 < numTrackParams && paramInfos[numGlobal + i + 1].type == pt_byte) {
				bridgeTrackVelSlot = i + 1;
			}
			break;
		}
	}

	{
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "[BuzzBridge] Note slots: gNote=%d gVel=%d tNote=%d tVel=%d\n",
			bridgeGlobalNoteSlot, bridgeGlobalVelSlot, bridgeTrackNoteSlot, bridgeTrackVelSlot);
		OutputDebugStringA(dbg);
	}

	firstTick = true;
	samplesUntilNextTick = 0;
	machineReady = true;
	return true;
}

#else // 32-bit direct mode

bool BuzzProcessor::loadBuzzMachine(const std::string& path)
{
	machineReady = false;

	if (path.empty()) {
		OutputDebugStringA("[BuzzBridge] loadBuzzMachine: empty path\n");
		return false;
	}

	OutputDebugStringA("[BuzzBridge] loadBuzzMachine: ");
	OutputDebugStringA(path.c_str());
	OutputDebugStringA("\n");

	if (!loader.Load(path.c_str())) {
		OutputDebugStringA("[BuzzBridge] loader.Load() failed\n");
		return false;
	}

	const CMachineInfo* info = loader.GetInfo();
	if (!info) {
		OutputDebugStringA("[BuzzBridge] GetInfo returned null\n");
		loader.Unload();
		return false;
	}
	if (!acceptsMachineType(info->Type)) {
		char msg[128];
		snprintf(msg, sizeof(msg), "[BuzzBridge] Machine type %d not accepted\n", info->Type);
		OutputDebugStringA(msg);
		loader.Unload();
		return false;
	}

	machineType = info->Type;
	machineFlags = info->Flags;

	loader.UpdateMasterInfo(125.0, processSetup.sampleRate);

	if (!loader.InitMachine()) {
		loader.Unload();
		return false;
	}

	auto* layout = loader.GetParamLayout();
	auto& gSlots = layout->GetGlobalSlots();
	auto& tSlots = layout->GetTrackSlots();
	auto* minfo = loader.GetInfo();

	// Store track limits from machine info
	machineMinTracks = minfo ? minfo->minTracks : 0;
	machineMaxTracks = minfo ? minfo->maxTracks : 0;
	numTrackParams = (int)tSlots.size();
	numTracks = std::max(1, machineMinTracks);

	currentGlobalValues.resize(gSlots.size());
	globalParamChanged.resize(gSlots.size(), true);

	for (int i = 0; i < (int)gSlots.size(); i++) {
		if (gSlots[i].param->Flags & MPF_STATE) {
			currentGlobalValues[i] = gSlots[i].param->DefValue;
		} else {
			currentGlobalValues[i] = gSlots[i].param->NoValue;
		}
	}

	// Initialize track parameter arrays for all tracks
	currentTrackValues.resize(numTracks);
	trackParamChanged.resize(numTracks);
	for (int t = 0; t < numTracks; t++) {
		currentTrackValues[t].resize(tSlots.size());
		trackParamChanged[t].resize(tSlots.size(), true);
		for (int i = 0; i < (int)tSlots.size(); i++) {
			if (tSlots[i].param->Flags & MPF_STATE) {
				currentTrackValues[t][i] = tSlots[i].param->DefValue;
			} else {
				currentTrackValues[t][i] = tSlots[i].param->NoValue;
			}
		}
	}

	firstTick = true;
	samplesUntilNextTick = 0;
	machineReady = true;

	return true;
}

#endif

tresult PLUGIN_API BuzzProcessor::setState(IBStream* state)
{
	if (!state) return kResultFalse;

	IBStreamer streamer(state, kLittleEndian);

	// Step 1: Read all saved data from the stream into temporaries.
	// We must read everything before loading the machine, because
	// loadBuzzMachine() initializes the param arrays with defaults.

	// Read DLL path (length-prefixed string, max 1023 chars)
	char pathBuf[1024] = {0};
	Steinberg::int32 pathLen = 0;
	if (!streamer.readInt32(pathLen)) return kResultFalse;
	if (pathLen < 0 || pathLen >= 1024) pathLen = 0; // reject invalid lengths
	if (pathLen > 0) {
		if (streamer.readRaw(pathBuf, pathLen) != pathLen) return kResultFalse;
		pathBuf[pathLen] = 0;
	}

	std::string savedDllPath = pathBuf;

	// Read bypass state (0 or 1, but accept any value)
	Steinberg::int32 bypass = 0;
	if (!streamer.readInt32(bypass)) return kResultFalse;
	bBypass = (bypass != 0);

	// Read saved global parameter values
	std::vector<Steinberg::int32> savedGlobalValues;
	Steinberg::int32 numGlobal = 0;
	if (streamer.readInt32(numGlobal)) {
		// Clamp to sane range to prevent malicious allocation
		if (numGlobal < 0) numGlobal = 0;
		if (numGlobal > kMaxGlobalParams) numGlobal = kMaxGlobalParams;
		if (numGlobal > 0) {
			savedGlobalValues.resize(numGlobal);
			for (Steinberg::int32 i = 0; i < numGlobal; i++) {
				if (!streamer.readInt32(savedGlobalValues[i])) break;
			}
		}
	}

	// Read track params: numTracks, numParamsPerTrack, then values
	Steinberg::int32 savedNumTracks = 0;
	Steinberg::int32 savedNumTrackParams = 0;
	std::vector<std::vector<Steinberg::int32>> savedTrackValues;
	if (streamer.readInt32(savedNumTracks) && streamer.readInt32(savedNumTrackParams)) {
		// Validate both counts against hard limits
		if (savedNumTracks < 0) savedNumTracks = 0;
		if (savedNumTracks > kMaxTracks) savedNumTracks = 0; // reject, don't clamp
		if (savedNumTrackParams < 0) savedNumTrackParams = 0;
		if (savedNumTrackParams > kMaxTrackParams) savedNumTrackParams = 0;

		if (savedNumTracks > 0 && savedNumTrackParams > 0) {
			savedTrackValues.resize(savedNumTracks);
			for (Steinberg::int32 t = 0; t < savedNumTracks; t++) {
				savedTrackValues[t].resize(savedNumTrackParams);
				for (Steinberg::int32 i = 0; i < savedNumTrackParams; i++) {
					if (!streamer.readInt32(savedTrackValues[t][i])) break;
				}
			}
		}
	}

	// Step 2: Load the machine (this sets param arrays to defaults)
	dllPath = savedDllPath;
	if (!dllPath.empty() && !machineReady) {
		if (loadBuzzMachine(dllPath)) {
			OutputDebugStringA("[BuzzBridge] Processor::setState: machine loaded, notifying controller\n");
			sendMachineLoadedToController();
		}
	}

	// Restore saved track count
	if (savedNumTracks > 0 && savedNumTracks <= machineMaxTracks) {
		numTracks = savedNumTracks;
		currentTrackValues.resize(numTracks);
		trackParamChanged.resize(numTracks);
#ifndef BUZZVST_64BIT
		if (auto* machine = loader.GetMachine()) {
			SEH_Call([&]() { machine->SetNumTracks(numTracks); });
		}
#endif
	}

	// Step 3: Overwrite defaults with saved values
	for (Steinberg::int32 i = 0; i < (Steinberg::int32)savedGlobalValues.size() &&
	     i < (Steinberg::int32)currentGlobalValues.size(); i++) {
		currentGlobalValues[i] = savedGlobalValues[i];
		globalParamChanged[i] = true;
	}

	for (Steinberg::int32 t = 0; t < (Steinberg::int32)savedTrackValues.size() &&
	     t < (Steinberg::int32)currentTrackValues.size(); t++) {
		for (Steinberg::int32 i = 0; i < (Steinberg::int32)savedTrackValues[t].size() &&
		     i < (Steinberg::int32)currentTrackValues[t].size(); i++) {
			currentTrackValues[t][i] = savedTrackValues[t][i];
			trackParamChanged[t][i] = true;
		}
	}

	firstTick = true; // Ensure saved values get sent on next tick

	// Restore wave table
	Steinberg::int32 numWaves = 0;
	if (streamer.readInt32(numWaves) && numWaves > 0) {
		// Clamp to sane range (max 200 wave slots in Buzz)
		if (numWaves > kMaxWaveSlots) numWaves = kMaxWaveSlots;

		auto* waveTable = loader.GetWaveTable();
		if (waveTable) {
			for (Steinberg::int32 i = 0; i < numWaves; i++) {
				Steinberg::int32 slotIdx = 0;
				if (!streamer.readInt32(slotIdx)) break;

				Steinberg::int32 pLen = 0;
				if (!streamer.readInt32(pLen)) break;

				// Validate slot index and path length
				if (slotIdx < WAVE_MIN || slotIdx > WAVE_MAX) {
					// Skip this entry's path data
					if (pLen > 0 && pLen < 32768) {
						char skip[32768];
						streamer.readRaw(skip, pLen);
					}
					continue;
				}
				if (pLen <= 0 || pLen >= 4096) continue;

				char wavBuf[4096] = {};
				if (streamer.readRaw(wavBuf, pLen) == pLen) {
					wavBuf[pLen] = 0;
					waveTable->LoadWav(slotIdx, wavBuf);
				}
			}
		}
	}

	return kResultOk;
}

tresult PLUGIN_API BuzzProcessor::getState(IBStream* state)
{
	if (!state) return kResultFalse;

	IBStreamer streamer(state, kLittleEndian);

	// Write DLL path
	Steinberg::int32 pathLen = (Steinberg::int32)dllPath.size();
	streamer.writeInt32(pathLen);
	if (pathLen > 0) {
		streamer.writeRaw((void*)dllPath.c_str(), pathLen);
	}

	// Write bypass state
	streamer.writeInt32(bBypass ? 1 : 0);

	// Write parameter values
	Steinberg::int32 numGlobal = (Steinberg::int32)currentGlobalValues.size();
	streamer.writeInt32(numGlobal);
	for (Steinberg::int32 i = 0; i < numGlobal; i++) {
		streamer.writeInt32(currentGlobalValues[i]);
	}

	// Write track params: numTracks, numParamsPerTrack, then values for each track
	Steinberg::int32 nTracks = numTracks;
	Steinberg::int32 nTrackParams = numTrackParams;
	streamer.writeInt32(nTracks);
	streamer.writeInt32(nTrackParams);
	for (Steinberg::int32 t = 0; t < nTracks && t < (Steinberg::int32)currentTrackValues.size(); t++) {
		for (Steinberg::int32 i = 0; i < nTrackParams && i < (Steinberg::int32)currentTrackValues[t].size(); i++) {
			streamer.writeInt32(currentTrackValues[t][i]);
		}
	}

	// Write wave table paths
	auto wavePaths = loader.GetWaveTable()->GetLoadedPaths();
	Steinberg::int32 numWaves = (Steinberg::int32)wavePaths.size();
	streamer.writeInt32(numWaves);
	for (auto& wp : wavePaths) {
		streamer.writeInt32(wp.slotIndex);
		Steinberg::int32 pLen = (Steinberg::int32)wp.filePath.size();
		streamer.writeInt32(pLen);
		if (pLen > 0)
			streamer.writeRaw((void*)wp.filePath.c_str(), pLen);
	}

	return kResultOk;
}

void BuzzProcessor::sendMachineLoadedToController()
{
	OutputDebugStringA("[BuzzBridge] Processor: sending BuzzMachineLoaded to controller\n");
	auto msg = owned(allocateMessage());
	if (!msg) return;

	msg->setMessageID("BuzzMachineLoaded");
	msg->getAttributes()->setBinary("Path",
		dllPath.c_str(), (Steinberg::uint32)dllPath.size());

	// Machine name
	std::string machineName;
#ifdef BUZZVST_64BIT
	machineName = bridgeMachineName;
#else
	auto* mInfo = loader.GetInfo();
	if (mInfo && mInfo->Name) machineName = mInfo->Name;
#endif
	if (!machineName.empty()) {
		msg->getAttributes()->setBinary("Name",
			machineName.c_str(), (Steinberg::uint32)machineName.size());
	}

	msg->getAttributes()->setInt("MinTracks", machineMinTracks);
	msg->getAttributes()->setInt("MaxTracks", machineMaxTracks);
	msg->getAttributes()->setInt("NumGlobal", (int)currentGlobalValues.size());
	msg->getAttributes()->setInt("NumTrackParams", numTrackParams);

	// Encode parameter info blob
	std::vector<char> paramBlob;
	auto appendInt = [&](int32_t v) {
		paramBlob.insert(paramBlob.end(), (char*)&v, (char*)&v + 4);
	};

#ifdef BUZZVST_64BIT
	int32_t totalParams = (int32_t)bridgeParamInfos.size();
	appendInt(totalParams);
	for (auto& pi : bridgeParamInfos) {
		appendInt(pi.type);
		appendInt(pi.minValue);
		appendInt(pi.maxValue);
		appendInt(pi.noValue);
		appendInt(pi.defValue);
		appendInt(pi.flags);
		int32_t nameLen = (int32_t)strnlen(pi.name, sizeof(pi.name));
		appendInt(nameLen);
		paramBlob.insert(paramBlob.end(), pi.name, pi.name + nameLen);
	}
#else
	auto* layout = loader.GetParamLayout();
	if (layout) {
		auto& gSlots = layout->GetGlobalSlots();
		auto& tSlots = layout->GetTrackSlots();
		int32_t totalParams = (int32_t)(gSlots.size() + tSlots.size());
		appendInt(totalParams);
		auto encodeParam = [&](const CMachineParameter* p) {
			appendInt(p->Type);
			appendInt(p->MinValue);
			appendInt(p->MaxValue);
			appendInt(p->NoValue);
			appendInt(p->DefValue);
			appendInt(p->Flags);
			const char* name = p->Name ? p->Name : "";
			int32_t nameLen = (int32_t)strlen(name);
			appendInt(nameLen);
			paramBlob.insert(paramBlob.end(), name, name + nameLen);
		};
		for (auto& s : gSlots) encodeParam(s.param);
		for (auto& s : tSlots) encodeParam(s.param);
	}
#endif

	if (!paramBlob.empty()) {
		msg->getAttributes()->setBinary("Params",
			paramBlob.data(), (Steinberg::uint32)paramBlob.size());
	}

	sendMessage(msg);
}

void BuzzProcessor::sendWaveSlotsToController()
{
	auto* waveTable = loader.GetWaveTable();
	if (!waveTable) return;

	// Build payload: numSlots (int32) + [nameLen (int32) + nameData]...
	// Send names for slots 1..max where max = last loaded slot or 16
	int maxSlot = 16;
	for (int i = kMaxWaveSlots; i >= 1; i--) {
		if (waveTable->IsLoaded(i)) { maxSlot = i; break; }
	}
	if (maxSlot < 16) maxSlot = 16;

	std::vector<char> payload;
	int32_t numSlots = maxSlot;
	payload.insert(payload.end(), (char*)&numSlots, (char*)&numSlots + sizeof(numSlots));

	for (int i = 1; i <= maxSlot; i++) {
		const char* name = waveTable->GetWaveName(i);
		int32_t nameLen = (int32_t)strlen(name);
		payload.insert(payload.end(), (char*)&nameLen, (char*)&nameLen + sizeof(nameLen));
		if (nameLen > 0)
			payload.insert(payload.end(), name, name + nameLen);
	}

	if (auto msg = owned(allocateMessage())) {
		msg->setMessageID("BuzzWaveSlots");
		msg->getAttributes()->setBinary("Slots", payload.data(), (Steinberg::uint32)payload.size());
		sendMessage(msg);
	}
}

tresult PLUGIN_API BuzzProcessor::notify(IMessage* message)
{
	if (!message) return kInvalidArgument;

	{
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "[BuzzBridge] Processor::notify received: %s\n",
			message->getMessageID());
		OutputDebugStringA(dbg);
	}

	// Handle track count change
	if (strcmp(message->getMessageID(), "BuzzSetNumTracks") == 0) {
		Steinberg::int64 newCount = 0;
		if (message->getAttributes()->getInt("Count", newCount) == kResultOk) {
			int n = (int)newCount;
			if (n >= machineMinTracks && n <= machineMaxTracks && n <= kMaxTracks && n != numTracks) {
				numTracks = n;

				// Resize track param arrays
				currentTrackValues.resize(numTracks);
				trackParamChanged.resize(numTracks);
				for (int t = 0; t < numTracks; t++) {
					if ((int)currentTrackValues[t].size() != numTrackParams) {
						currentTrackValues[t].resize(numTrackParams);
						trackParamChanged[t].resize(numTrackParams, true);
#ifdef BUZZVST_64BIT
						for (int i = 0; i < numTrackParams && i < (int)bridgeParamInfos.size(); i++) {
							auto& pi = bridgeParamInfos[(int)currentGlobalValues.size() + i];
							if (pi.flags & MPF_STATE) {
								currentTrackValues[t][i] = pi.defValue;
							} else {
								currentTrackValues[t][i] = pi.noValue;
							}
						}
#else
						auto* layout = loader.GetParamLayout();
						auto& tSlots = layout->GetTrackSlots();
						for (int i = 0; i < (int)tSlots.size(); i++) {
							if (tSlots[i].param->Flags & MPF_STATE) {
								currentTrackValues[t][i] = tSlots[i].param->DefValue;
							} else {
								currentTrackValues[t][i] = tSlots[i].param->NoValue;
							}
						}
#endif
					}
				}

				// Tell the machine about the new track count
#ifdef BUZZVST_64BIT
				bridge.SetNumTracks(numTracks);
#else
				if (auto* machine = loader.GetMachine()) {
					SEH_Call([&]() { machine->SetNumTracks(numTracks); });
				}
#endif
			}
		}
		return kResultOk;
	}

	// Handle DLL path change message from controller
	if (strcmp(message->getMessageID(), "BuzzDllPath") == 0) {
		OutputDebugStringA("[BuzzBridge] BuzzDllPath handler entered\n");
		const void* data = nullptr;
		Steinberg::uint32 size = 0;
		tresult br = message->getAttributes()->getBinary("Path", data, size);
		{
			char dbg[512];
			snprintf(dbg, sizeof(dbg), "[BuzzBridge] BuzzDllPath: getBinary=%d size=%u machineReady=%d dllPath='%s'\n",
				(int)br, (unsigned)size, (int)machineReady, dllPath.c_str());
			OutputDebugStringA(dbg);
		}
		if (br == kResultOk && size > 0) {
			std::string newPath((const char*)data, size);
			{
				char dbg[512];
				snprintf(dbg, sizeof(dbg), "[BuzzBridge] BuzzDllPath: newPath='%s' willLoad=%d\n",
					newPath.c_str(), (int)(newPath != dllPath || !machineReady));
				OutputDebugStringA(dbg);
			}
			if (newPath != dllPath || !machineReady) {
				dllPath = newPath;
				if (machineReady) {
#ifdef BUZZVST_64BIT
					bridge.Unload();
#else
					loader.Unload();
#endif
					machineReady = false;
				}
				OutputDebugStringA("[BuzzBridge] Processor: loading machine...\n");
				if (loadBuzzMachine(dllPath)) {
					OutputDebugStringA("[BuzzBridge] Processor: machine loaded OK\n");
					sendMachineLoadedToController();
				} else {
					OutputDebugStringA("[BuzzBridge] Processor: loadBuzzMachine FAILED\n");
					// Notify controller that load failed
					if (auto msg = owned(allocateMessage())) {
						msg->setMessageID("BuzzMachineLoadFailed");
						msg->getAttributes()->setBinary("Path",
							dllPath.c_str(), (Steinberg::uint32)dllPath.size());
						sendMessage(msg);
					}
				}
			}
		}
		return kResultOk;
	}

	// Handle wave loading from controller
	if (strcmp(message->getMessageID(), "BuzzLoadWaves") == 0) {
		const void* data = nullptr;
		Steinberg::uint32 size = 0;
		if (message->getAttributes()->getBinary("Waves", data, size) == kResultOk && size >= 4) {
			const char* ptr = (const char*)data;
			int32_t numPaths = *(const int32_t*)ptr;
			ptr += sizeof(int32_t);

			auto* waveTable = loader.GetWaveTable();
			if (waveTable) {
				for (int32_t i = 0; i < numPaths; i++) {
					if ((ptr - (const char*)data) + 4 > (int)size) break;
					int32_t pathLen = *(const int32_t*)ptr;
					ptr += sizeof(int32_t);
					if (pathLen <= 0 || (ptr - (const char*)data) + pathLen > (int)size) break;

					std::string wavPath(ptr, pathLen);
					ptr += pathLen;

					waveTable->LoadWavAuto(wavPath);
				}
				sendWaveSlotsToController();
			}
		}
		return kResultOk;
	}

	// Handle single slot load/replace
	if (strcmp(message->getMessageID(), "BuzzLoadWaveSlot") == 0) {
		const void* data = nullptr;
		Steinberg::uint32 size = 0;
		if (message->getAttributes()->getBinary("Wave", data, size) == kResultOk && size >= 12) {
			const char* ptr = (const char*)data;
			int32_t num = *(const int32_t*)ptr; ptr += 4;
			int32_t slotIdx = *(const int32_t*)ptr; ptr += 4;
			int32_t pathLen = *(const int32_t*)ptr; ptr += 4;

			if (pathLen > 0 && (int)(ptr - (const char*)data) + pathLen <= (int)size) {
				std::string wavPath(ptr, pathLen);
				auto* waveTable = loader.GetWaveTable();
				if (waveTable) {
					waveTable->LoadWav(slotIdx, wavPath);
					sendWaveSlotsToController();
				}
			}
		}
		return kResultOk;
	}

	return AudioEffect::notify(message);
}

} // namespace BuzzVst
