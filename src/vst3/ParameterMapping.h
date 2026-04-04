#pragma once

#include "../buzz/MachineInterface.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace BuzzVst {

// Convert between VST3 normalized parameter values (0.0-1.0)
// and Buzz integer parameter values (MinValue-MaxValue).
class ParameterMapping {
public:
	// Convert VST3 normalized (0.0-1.0) to Buzz integer value
	static int NormalizedToBuzz(double normalized, const CMachineParameter* param);

	// Convert Buzz integer value to VST3 normalized (0.0-1.0)
	static double BuzzToNormalized(int buzzValue, const CMachineParameter* param);

	// Get the default normalized value for a Buzz parameter
	static double GetDefaultNormalized(const CMachineParameter* param);

	// Get step count for VST3 parameter (discrete steps)
	static Steinberg::int32 GetStepCount(const CMachineParameter* param);
};

} // namespace BuzzVst
