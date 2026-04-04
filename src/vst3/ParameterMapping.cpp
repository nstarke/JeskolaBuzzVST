#include "ParameterMapping.h"
#include <algorithm>
#include <cmath>

namespace BuzzVst {

int ParameterMapping::NormalizedToBuzz(double normalized, const CMachineParameter* param)
{
	if (!param) return 0;

	// Clamp to 0.0-1.0
	normalized = std::max(0.0, std::min(1.0, normalized));

	double range = (double)(param->MaxValue - param->MinValue);
	int buzzVal = param->MinValue + (int)(normalized * range + 0.5);

	// Clamp to valid range
	buzzVal = std::max(param->MinValue, std::min(param->MaxValue, buzzVal));

	return buzzVal;
}

double ParameterMapping::BuzzToNormalized(int buzzValue, const CMachineParameter* param)
{
	if (!param) return 0.0;

	int range = param->MaxValue - param->MinValue;
	if (range <= 0) return 0.0;

	double normalized = (double)(buzzValue - param->MinValue) / (double)range;
	return std::max(0.0, std::min(1.0, normalized));
}

double ParameterMapping::GetDefaultNormalized(const CMachineParameter* param)
{
	if (!param) return 0.0;

	// Only state parameters have meaningful defaults
	if (param->Flags & MPF_STATE) {
		return BuzzToNormalized(param->DefValue, param);
	}

	return 0.0;
}

Steinberg::int32 ParameterMapping::GetStepCount(const CMachineParameter* param)
{
	if (!param) return 0;

	// Return discrete step count (MaxValue - MinValue)
	return param->MaxValue - param->MinValue;
}

} // namespace BuzzVst
