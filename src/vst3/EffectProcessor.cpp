#include "EffectProcessor.h"
#include "plugids.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace BuzzVst {

EffectProcessor::EffectProcessor()
{
	setControllerClass(kBuzzEffectControllerUID);
}

void EffectProcessor::setupBuses()
{
	addAudioInput(STR16("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
	addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);
}

bool EffectProcessor::acceptsMachineType(int type) const
{
	return type == MT_EFFECT;
}

tresult PLUGIN_API EffectProcessor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                        SpeakerArrangement* outputs, int32 numOuts)
{
	if (numIns == 1 && numOuts == 1) {
		auto inCount = SpeakerArr::getChannelCount(inputs[0]);
		auto outCount = SpeakerArr::getChannelCount(outputs[0]);

		if ((inCount == 1 || inCount == 2) && (outCount == 1 || outCount == 2)) {
			getAudioInput(0)->setArrangement(inputs[0]);
			getAudioOutput(0)->setArrangement(outputs[0]);
			return kResultOk;
		}
	}
	return kResultFalse;
}

void EffectProcessor::processAudioBlock(float** inputs, float** outputs,
                                          int32 numInputChannels, int32 numOutputChannels,
                                          int32 numSamples)
{
	if (!machineReady || !outputs || numOutputChannels < 1 || numSamples <= 0)
		return;

	if (bBypass) {
		for (int32 ch = 0; ch < std::min(numInputChannels, numOutputChannels); ch++) {
			if (inputs && inputs[ch] && outputs[ch])
				memcpy(outputs[ch], inputs[ch], numSamples * sizeof(float));
		}
		return;
	}

	// Effects always use WM_READWRITE: input is in the buffer (READ) and
	// the machine writes processed output back (WRITE).
	bool hasInput = (inputs && numInputChannels > 0 && inputs[0]);
	int workMode = hasInput ? WM_READWRITE : WM_NOIO;

	// --- Effect diagnostic logging (first call + periodic ~1s interval) ---
	{
		static int effectDiagSamples = 0;
		static int effectDiagCalls = 0;
		static bool effectFirstLog = true;
		effectDiagSamples += numSamples;
		effectDiagCalls++;

		// Log on first call and approximately once per second
		if (effectFirstLog || effectDiagSamples >= 44100) {
			effectFirstLog = false;
			float inPeakL = 0, inPeakR = 0;
			if (hasInput && inputs[0]) {
				for (int32 i = 0; i < numSamples; i++) {
					float a = fabsf(inputs[0][i]);
					if (a > inPeakL) inPeakL = a;
				}
			}
			if (hasInput && numInputChannels >= 2 && inputs[1]) {
				for (int32 i = 0; i < numSamples; i++) {
					float a = fabsf(inputs[1][i]);
					if (a > inPeakR) inPeakR = a;
				}
			}

			char dbg[512];
			snprintf(dbg, sizeof(dbg),
				"[BuzzBridgeEffect DIAG] blockSize=%d calls/sec=%d inCh=%d outCh=%d "
				"hasInput=%d workMode=%d inPeakL=%.4f inPeakR=%.4f "
				"machineType=%d flags=0x%X shouldResample=%d hostRate=%.0f\n",
				numSamples, effectDiagCalls, numInputChannels, numOutputChannels,
				(int)hasInput, workMode, inPeakL, inPeakR,
				machineType, machineFlags, (int)shouldResample(),
				processSetup.sampleRate);
			OutputDebugStringA(dbg);

			effectDiagSamples = 0;
			effectDiagCalls = 0;
		}
	}

#ifdef BUZZVST_64BIT
	auto* audio = bridge.GetAudio();
	if (!audio) {
		for (int32 ch = 0; ch < numOutputChannels; ch++)
			if (outputs[ch]) memset(outputs[ch], 0, numSamples * sizeof(float));
		return;
	}

	// Copy input to shared memory (scaled to Buzz range)
	if (hasInput) {
		for (int32 i = 0; i < numSamples; i++)
			audio->inputLeft[i] = inputs[0][i] * kVst3ToBuzzScale;
	} else {
		memset(audio->inputLeft, 0, numSamples * sizeof(float));
	}
	if (inputs && numInputChannels >= 2 && inputs[1]) {
		for (int32 i = 0; i < numSamples; i++)
			audio->inputRight[i] = inputs[1][i] * kVst3ToBuzzScale;
	} else {
		memcpy(audio->inputRight, audio->inputLeft, numSamples * sizeof(float));
	}

	// Measure input peak before sending to bridge
	float bridgeInPeakL = 0, bridgeInPeakR = 0;
	{
		static int bridgeDiag = 0;
		if (bridgeDiag == 0 || bridgeDiag % 500 == 0) {
			for (int32 i = 0; i < numSamples; i++) {
				float al = fabsf(audio->inputLeft[i]);
				float ar = fabsf(audio->inputRight[i]);
				if (al > bridgeInPeakL) bridgeInPeakL = al;
				if (ar > bridgeInPeakR) bridgeInPeakR = ar;
			}
		}
		bridgeDiag++;
	}

	// The bridge host handles mono-to-stereo based on machine flags internally
	bool workOk = bridge.Work(numSamples, workMode);
	{
		static int effectLog = 0;
		if (effectLog == 0 || effectLog % 500 == 0) {
			float outPeakL = 0, outPeakR = 0;
			if (workOk && audio->hasOutput) {
				for (int32 i = 0; i < numSamples; i++) {
					float al = fabsf(audio->outputLeft[i]);
					float ar = fabsf(audio->outputRight[i]);
					if (al > outPeakL) outPeakL = al;
					if (ar > outPeakR) outPeakR = ar;
				}
			}
			char dbg[512];
			snprintf(dbg, sizeof(dbg),
				"[BuzzBridgeEffect BRIDGE] n=%d mode=%d ok=%d hasOut=%d "
				"buzzInPeakL=%.1f buzzInPeakR=%.1f buzzOutPeakL=%.1f buzzOutPeakR=%.1f "
				"shouldResample=%d hostRate=%.0f\n",
				numSamples, workMode, (int)workOk, audio->hasOutput,
				bridgeInPeakL, bridgeInPeakR, outPeakL, outPeakR,
				(int)shouldResample(), processSetup.sampleRate);
			OutputDebugStringA(dbg);
		}
		effectLog++;
	}
	if (workOk && audio->hasOutput) {
		for (int32 i = 0; i < numSamples; i++) {
			outputs[0][i] = BuzzSampleToVst3(audio->outputLeft[i]);
			if (numOutputChannels >= 2)
				outputs[1][i] = BuzzSampleToVst3(audio->outputRight[i]);
		}
	} else if (hasInput) {
		// Effect produced no output — pass input through (transparent)
		for (int32 ch = 0; ch < std::min(numInputChannels, numOutputChannels); ch++) {
			if (inputs[ch] && outputs[ch])
				memcpy(outputs[ch], inputs[ch], numSamples * sizeof(float));
		}
	} else {
		for (int32 ch = 0; ch < numOutputChannels; ch++)
			if (outputs[ch]) memset(outputs[ch], 0, numSamples * sizeof(float));
	}

#else
	// 32-bit direct mode
	auto* machine = loader.GetMachine();
	auto* info = loader.GetInfo();
	if (!machine || !info) return;

	bool stereoEffect = (info->Flags & MIF_STEREO_EFFECT) != 0;
	bool monoToStereo = (info->Flags & MIF_MONO_TO_STEREO) != 0;

	// --- Effect Work() diagnostics (periodic) ---
	static int workDiagCounter = 0;
	bool logThisCall = (workDiagCounter == 0 || workDiagCounter % 500 == 0);
	workDiagCounter++;

	if (monoToStereo && numOutputChannels >= 2) {
		if (inputs && inputs[0]) {
			for (int32 i = 0; i < numSamples; i++)
				workBufLeft[i] = inputs[0][i] * kVst3ToBuzzScale;
		} else {
			memset(workBufLeft, 0, numSamples * sizeof(float));
		}
		memset(workBufRight, 0, numSamples * sizeof(float));

		float buzzInPeak = 0;
		if (logThisCall) {
			for (int32 i = 0; i < numSamples; i++) {
				float a = fabsf(workBufLeft[i]);
				if (a > buzzInPeak) buzzInPeak = a;
			}
		}

		bool hasOutput = false;
		SEH_Call([&]() {
			hasOutput = machine->WorkMonoToStereo(workBufLeft, workBufRight, numSamples, workMode);
		});

		if (logThisCall) {
			float buzzOutPeakL = 0, buzzOutPeakR = 0;
			for (int32 i = 0; i < numSamples; i++) {
				float al = fabsf(workBufLeft[i]);
				float ar = fabsf(workBufRight[i]);
				if (al > buzzOutPeakL) buzzOutPeakL = al;
				if (ar > buzzOutPeakR) buzzOutPeakR = ar;
			}
			char dbg[512];
			snprintf(dbg, sizeof(dbg),
				"[BuzzBridgeEffect WORK] path=MonoToStereo n=%d mode=%d hasOut=%d "
				"buzzInPeak=%.1f buzzOutPeakL=%.1f buzzOutPeakR=%.1f "
				"masterSR=%d masterSPT=%d\n",
				numSamples, workMode, (int)hasOutput,
				buzzInPeak, buzzOutPeakL, buzzOutPeakR,
				loader.GetMasterInfo()->SamplesPerSec,
				loader.GetMasterInfo()->SamplesPerTick);
			OutputDebugStringA(dbg);
		}

		if (hasOutput) {
			for (int32 i = 0; i < numSamples; i++) {
				outputs[0][i] = BuzzSampleToVst3(workBufLeft[i]);
				if (numOutputChannels >= 2)
					outputs[1][i] = BuzzSampleToVst3(workBufRight[i]);
			}
		} else if (hasInput) {
			for (int32 ch = 0; ch < std::min(numInputChannels, numOutputChannels); ch++) {
				if (inputs[ch] && outputs[ch])
					memcpy(outputs[ch], inputs[ch], numSamples * sizeof(float));
			}
		} else {
			for (int32 ch = 0; ch < numOutputChannels; ch++)
				if (outputs[ch]) memset(outputs[ch], 0, numSamples * sizeof(float));
		}
	} else if (stereoEffect && numInputChannels >= 2 && numOutputChannels >= 2) {
		if (inputs && inputs[0]) {
			for (int32 i = 0; i < numSamples; i++)
				workBufLeft[i] = inputs[0][i] * kVst3ToBuzzScale;
		} else {
			memset(workBufLeft, 0, numSamples * sizeof(float));
		}

		float buzzInPeakL = 0;
		if (logThisCall) {
			for (int32 i = 0; i < numSamples; i++) {
				float a = fabsf(workBufLeft[i]);
				if (a > buzzInPeakL) buzzInPeakL = a;
			}
		}

		bool hasOutputL = false;
		SEH_Call([&]() { hasOutputL = machine->Work(workBufLeft, numSamples, workMode); });

		if (inputs && inputs[1]) {
			for (int32 i = 0; i < numSamples; i++)
				workBufRight[i] = inputs[1][i] * kVst3ToBuzzScale;
		} else {
			memset(workBufRight, 0, numSamples * sizeof(float));
		}

		float buzzInPeakR = 0;
		if (logThisCall) {
			for (int32 i = 0; i < numSamples; i++) {
				float a = fabsf(workBufRight[i]);
				if (a > buzzInPeakR) buzzInPeakR = a;
			}
		}

		bool hasOutputR = false;
		SEH_Call([&]() { hasOutputR = machine->Work(workBufRight, numSamples, workMode); });

		if (logThisCall) {
			float buzzOutPeakL = 0, buzzOutPeakR = 0;
			for (int32 i = 0; i < numSamples; i++) {
				float al = fabsf(workBufLeft[i]);
				float ar = fabsf(workBufRight[i]);
				if (al > buzzOutPeakL) buzzOutPeakL = al;
				if (ar > buzzOutPeakR) buzzOutPeakR = ar;
			}
			char dbg[512];
			snprintf(dbg, sizeof(dbg),
				"[BuzzBridgeEffect WORK] path=Stereo n=%d mode=%d hasOutL=%d hasOutR=%d "
				"buzzInPeakL=%.1f buzzInPeakR=%.1f buzzOutPeakL=%.1f buzzOutPeakR=%.1f "
				"masterSR=%d masterSPT=%d\n",
				numSamples, workMode, (int)hasOutputL, (int)hasOutputR,
				buzzInPeakL, buzzInPeakR, buzzOutPeakL, buzzOutPeakR,
				loader.GetMasterInfo()->SamplesPerSec,
				loader.GetMasterInfo()->SamplesPerTick);
			OutputDebugStringA(dbg);
		}

		for (int32 i = 0; i < numSamples; i++) {
			outputs[0][i] = hasOutputL ? BuzzSampleToVst3(workBufLeft[i]) : 0.0f;
			outputs[1][i] = hasOutputR ? BuzzSampleToVst3(workBufRight[i]) : 0.0f;
		}
	} else {
		if (inputs && inputs[0]) {
			for (int32 i = 0; i < numSamples; i++)
				workBufLeft[i] = inputs[0][i] * kVst3ToBuzzScale;
		} else {
			memset(workBufLeft, 0, numSamples * sizeof(float));
		}

		float buzzInPeak = 0;
		if (logThisCall) {
			for (int32 i = 0; i < numSamples; i++) {
				float a = fabsf(workBufLeft[i]);
				if (a > buzzInPeak) buzzInPeak = a;
			}
		}

		bool hasOutput = false;
		SEH_Call([&]() { hasOutput = machine->Work(workBufLeft, numSamples, workMode); });

		if (logThisCall) {
			float buzzOutPeak = 0;
			for (int32 i = 0; i < numSamples; i++) {
				float a = fabsf(workBufLeft[i]);
				if (a > buzzOutPeak) buzzOutPeak = a;
			}
			char dbg[512];
			snprintf(dbg, sizeof(dbg),
				"[BuzzBridgeEffect WORK] path=Mono n=%d mode=%d hasOut=%d "
				"buzzInPeak=%.1f buzzOutPeak=%.1f "
				"masterSR=%d masterSPT=%d\n",
				numSamples, workMode, (int)hasOutput,
				buzzInPeak, buzzOutPeak,
				loader.GetMasterInfo()->SamplesPerSec,
				loader.GetMasterInfo()->SamplesPerTick);
			OutputDebugStringA(dbg);
		}

		if (hasOutput) {
			for (int32 i = 0; i < numSamples; i++) {
				float sample = BuzzSampleToVst3(workBufLeft[i]);
				for (int32 ch = 0; ch < numOutputChannels; ch++)
					if (outputs[ch]) outputs[ch][i] = sample;
			}
		} else if (hasInput) {
			for (int32 ch = 0; ch < std::min(numInputChannels, numOutputChannels); ch++) {
				if (inputs[ch] && outputs[ch])
					memcpy(outputs[ch], inputs[ch], numSamples * sizeof(float));
			}
		} else {
			for (int32 ch = 0; ch < numOutputChannels; ch++)
				if (outputs[ch]) memset(outputs[ch], 0, numSamples * sizeof(float));
		}
	}
#endif
}

} // namespace BuzzVst
