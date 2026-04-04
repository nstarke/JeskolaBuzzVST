#pragma once

#include "BuzzProcessor.h"

namespace BuzzVst {

class GeneratorProcessor : public BuzzProcessor {
public:
	GeneratorProcessor();

	static FUnknown* createInstance(void*) { return (IAudioProcessor*)new GeneratorProcessor; }

protected:
	void setupBuses() override;
	void processAudioBlock(float** inputs, float** outputs,
	                        int32 numInputChannels, int32 numOutputChannels,
	                        int32 numSamples) override;
	bool acceptsMachineType(int type) const override;
};

} // namespace BuzzVst
