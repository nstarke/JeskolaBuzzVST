#pragma once

#include "BuzzProcessor.h"

namespace BuzzVst {

class EffectProcessor : public BuzzProcessor {
public:
	EffectProcessor();

	static FUnknown* createInstance(void*) { return (IAudioProcessor*)new EffectProcessor; }

	tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
	                                       SpeakerArrangement* outputs, int32 numOuts) SMTG_OVERRIDE;

protected:
	void setupBuses() override;
	void processAudioBlock(float** inputs, float** outputs,
	                        int32 numInputChannels, int32 numOutputChannels,
	                        int32 numSamples) override;
	bool acceptsMachineType(int type) const override;
};

} // namespace BuzzVst
