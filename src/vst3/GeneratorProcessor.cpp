#include "GeneratorProcessor.h"
#include "plugids.h"
#include <cstring>
#include <algorithm>

namespace BuzzVst {

GeneratorProcessor::GeneratorProcessor()
{
	setControllerClass(kBuzzGeneratorControllerUID);
}

void GeneratorProcessor::setupBuses()
{
	addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);
}

bool GeneratorProcessor::acceptsMachineType(int type) const
{
	return type == MT_GENERATOR;
}

void GeneratorProcessor::processAudioBlock(float** inputs, float** outputs,
                                            int32 numInputChannels, int32 numOutputChannels,
                                            int32 numSamples)
{
	if (!machineReady || !outputs || numOutputChannels < 1 || numSamples <= 0)
		return;

	if (bBypass) {
		for (int32 ch = 0; ch < numOutputChannels; ch++) {
			if (outputs[ch]) memset(outputs[ch], 0, numSamples * sizeof(float));
		}
		return;
	}

#ifdef BUZZVST_64BIT
	// 64-bit: send audio through bridge
	auto* audio = bridge.GetAudio();
	if (!audio) {
		static int noAudioLog = 0;
		if (noAudioLog++ < 3) OutputDebugStringA("[BuzzBridge] processAudio: GetAudio() returned null\n");
		for (int32 ch = 0; ch < numOutputChannels; ch++)
			if (outputs[ch]) memset(outputs[ch], 0, numSamples * sizeof(float));
		return;
	}

	if (bridge.Work(numSamples, WM_WRITE)) {
		if (audio->hasOutput) {
			static int gotOutputLog = 0;
			if (gotOutputLog++ < 3) {
				char dbg[128];
				snprintf(dbg, sizeof(dbg), "[BuzzBridge] processAudio: hasOutput=1 L[0]=%f\n", audio->outputLeft[0]);
				OutputDebugStringA(dbg);
			}
			for (int32 i = 0; i < numSamples; i++) {
				outputs[0][i] = BuzzSampleToVst3(audio->outputLeft[i]);
				if (numOutputChannels >= 2)
					outputs[1][i] = BuzzSampleToVst3(audio->outputRight[i]);
			}
		} else {
			static int noOutputLog = 0;
			if (noOutputLog++ < 3) OutputDebugStringA("[BuzzBridge] processAudio: Work OK but hasOutput=0\n");
			for (int32 ch = 0; ch < numOutputChannels; ch++)
				if (outputs[ch]) memset(outputs[ch], 0, numSamples * sizeof(float));
		}
	} else {
		static int workFailLog = 0;
		if (workFailLog++ < 3) OutputDebugStringA("[BuzzBridge] processAudio: Work() FAILED\n");
		for (int32 ch = 0; ch < numOutputChannels; ch++)
			if (outputs[ch]) memset(outputs[ch], 0, numSamples * sizeof(float));
	}

#else
	// 32-bit: direct machine call
	auto* machine = loader.GetMachine();
	auto* info = loader.GetInfo();
	if (!machine || !info) return;

	bool monoToStereo = (info->Flags & MIF_MONO_TO_STEREO) != 0;

	if (monoToStereo && numOutputChannels >= 2) {
		memset(workBufLeft, 0, numSamples * sizeof(float));
		memset(workBufRight, 0, numSamples * sizeof(float));

		bool hasOutput = false;
		SEH_Call([&]() {
			hasOutput = machine->WorkMonoToStereo(workBufLeft, workBufRight, numSamples, WM_WRITE);
		});

		if (hasOutput) {
			for (int32 i = 0; i < numSamples; i++) {
				outputs[0][i] = BuzzSampleToVst3(workBufLeft[i]);
				outputs[1][i] = BuzzSampleToVst3(workBufRight[i]);
			}
		} else {
			memset(outputs[0], 0, numSamples * sizeof(float));
			memset(outputs[1], 0, numSamples * sizeof(float));
		}
	} else {
		memset(workBufLeft, 0, numSamples * sizeof(float));

		bool hasOutput = false;
		SEH_Call([&]() {
			hasOutput = machine->Work(workBufLeft, numSamples, WM_WRITE);
		});

		if (hasOutput) {
			for (int32 i = 0; i < numSamples; i++) {
				float sample = BuzzSampleToVst3(workBufLeft[i]);
				for (int32 ch = 0; ch < numOutputChannels; ch++) {
					if (outputs[ch]) outputs[ch][i] = sample;
				}
			}
		} else {
			for (int32 ch = 0; ch < numOutputChannels; ch++) {
				if (outputs[ch]) memset(outputs[ch], 0, numSamples * sizeof(float));
			}
		}
	}
#endif
}

} // namespace BuzzVst
