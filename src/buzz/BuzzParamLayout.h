#pragma once

#include "MachineInterface.h"
#include <vector>
#include <string>

namespace BuzzVst {

struct ParamSlot {
	int paramIndex;
	int byteOffset;
	int byteSize;
	const CMachineParameter* param;
};

class BuzzParamLayout {
public:
	void Build(const CMachineInfo* info);

	void WriteGlobalParam(void* globalVals, int slotIndex, int value) const;
	int ReadGlobalParam(const void* globalVals, int slotIndex) const;
	void WriteAllNoValues(void* globalVals) const;
	void WriteAllDefaults(void* globalVals) const;

	void WriteTrackParam(void* trackVals, int trackIndex, int slotIndex, int value) const;
	int ReadTrackParam(const void* trackVals, int trackIndex, int slotIndex) const;
	void WriteTrackAllNoValues(void* trackVals, int numTracks) const;

	int GetGlobalStructSize() const { return globalStructSize; }
	int GetTrackStructSize() const { return trackStructSize; }
	int GetTotalParamCount() const { return (int)(globalSlots.size() + trackSlots.size()); }

	const std::vector<ParamSlot>& GetGlobalSlots() const { return globalSlots; }
	const std::vector<ParamSlot>& GetTrackSlots() const { return trackSlots; }

	static int GetParamByteSize(CMPType type);

private:
	std::vector<ParamSlot> globalSlots;
	std::vector<ParamSlot> trackSlots;
	int globalStructSize = 0;
	int trackStructSize = 0;
};

} // namespace BuzzVst
