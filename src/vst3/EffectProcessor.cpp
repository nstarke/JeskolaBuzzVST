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

	// Determine work mode
	bool hasInput = false;
	if (inputs && numInputChannels > 0 && inputs[0]) {
		for (int32 i = 0; i < numSamples; i++) {
			if (fabsf(inputs[0][i]) > 1e-10f) { hasInput = true; break; }
		}
	}
	int workMode = hasInput ? WM_READWRITE : WM_NOIO;

#ifdef BUZZVST_64BIT
	auto* audio = bridge.GetAudio();
	if (!audio) {
		for (int32 ch = 0; ch < numOutputChannels; ch++)
			if (outputs[ch]) memset(outputs[ch], 0, numSamples * sizeof(float));
		return;
	}

	// Copy input to shared memory (scaled to Buzz range)
	if (inputs && inputs[0]) {
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

	if (bridge.Work(numSamples, workMode) && audio->hasOutput) {
		for (int32 i = 0; i < numSamples; i++) {
			outputs[0][i] = audio->outputLeft[i] * kBuzzToVst3Scale;
			if (numOutputChannels >= 2)
				outputs[1][i] = audio->outputRight[i] * kBuzzToVst3Scale;
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

	if (monoToStereo && numOutputChannels >= 2) {
		if (inputs && inputs[0]) {
			for (int32 i = 0; i < numSamples; i++)
				workBufLeft[i] = inputs[0][i] * kVst3ToBuzzScale;
		} else {
			memset(workBufLeft, 0, numSamples * sizeof(float));
		}
		memset(workBufRight, 0, numSamples * sizeof(float));

		bool hasOutput = false;
		SEH_Call([&]() {
			hasOutput = machine->WorkMonoToStereo(workBufLeft, workBufRight, numSamples, workMode);
		});

		if (hasOutput) {
			for (int32 i = 0; i < numSamples; i++) {
				outputs[0][i] = BuzzSampleToVst3(workBufLeft[i]);
				if (numOutputChannels >= 2)
					outputs[1][i] = BuzzSampleToVst3(workBufRight[i]);
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

		bool hasOutputL = false;
		SEH_Call([&]() { hasOutputL = machine->Work(workBufLeft, numSamples, workMode); });

		if (inputs && inputs[1]) {
			for (int32 i = 0; i < numSamples; i++)
				workBufRight[i] = inputs[1][i] * kVst3ToBuzzScale;
		} else {
			memset(workBufRight, 0, numSamples * sizeof(float));
		}

		bool hasOutputR = false;
		SEH_Call([&]() { hasOutputR = machine->Work(workBufRight, numSamples, workMode); });

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

		bool hasOutput = false;
		SEH_Call([&]() { hasOutput = machine->Work(workBufLeft, numSamples, workMode); });

		if (hasOutput) {
			for (int32 i = 0; i < numSamples; i++) {
				float sample = BuzzSampleToVst3(workBufLeft[i]);
				for (int32 ch = 0; ch < numOutputChannels; ch++)
					if (outputs[ch]) outputs[ch][i] = sample;
			}
		} else {
			for (int32 ch = 0; ch < numOutputChannels; ch++)
				if (outputs[ch]) memset(outputs[ch], 0, numSamples * sizeof(float));
		}
	}
#endif
}

} // namespace BuzzVst
