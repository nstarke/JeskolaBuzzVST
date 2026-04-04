#include "BuzzParamLayout.h"
#include <climits>
#include <cstring>

namespace BuzzVst {

int BuzzParamLayout::GetParamByteSize(CMPType type)
{
	switch (type) {
		case pt_note:   return 1; // byte (note value 0-240 fits in one byte)
		case pt_switch: return 1; // byte
		case pt_byte:   return 1;
		case pt_word:   return 2;
		default:        return 0;
	}
}

void BuzzParamLayout::Build(const CMachineInfo* info)
{
	globalSlots.clear();
	trackSlots.clear();
	globalStructSize = 0;
	trackStructSize = 0;

	if (!info || !info->Parameters)
		return;

	// Global parameters come first
	int offset = 0;
	for (int i = 0; i < info->numGlobalParameters; i++) {
		const CMachineParameter* p = info->Parameters[i];
		if (!p) continue;

		int size = GetParamByteSize(p->Type);
		if (size == 0) continue;

		ParamSlot slot;
		slot.paramIndex = i;
		slot.byteOffset = offset;
		slot.byteSize = size;
		slot.param = p;
		globalSlots.push_back(slot);

		offset += size;
	}
	globalStructSize = offset;

	// Track parameters follow global parameters in the Parameters array
	offset = 0;
	for (int i = 0; i < info->numTrackParameters; i++) {
		int paramIdx = info->numGlobalParameters + i;
		if (paramIdx < 0 || paramIdx < info->numGlobalParameters) break; // overflow check
		const CMachineParameter* p = info->Parameters[paramIdx];
		if (!p) continue;

		int size = GetParamByteSize(p->Type);
		if (size == 0) continue;

		ParamSlot slot;
		slot.paramIndex = paramIdx;
		slot.byteOffset = offset;
		slot.byteSize = size;
		slot.param = p;
		trackSlots.push_back(slot);

		offset += size;
	}
	trackStructSize = offset;
}

void BuzzParamLayout::WriteGlobalParam(void* globalVals, int slotIndex, int value) const
{
	if (!globalVals || slotIndex < 0 || slotIndex >= (int)globalSlots.size())
		return;

	const ParamSlot& slot = globalSlots[slotIndex];
	unsigned char* ptr = (unsigned char*)globalVals + slot.byteOffset;

	if (slot.byteSize == 1) {
		*ptr = (unsigned char)(value & 0xFF);
	} else if (slot.byteSize == 2) {
		*(unsigned short*)ptr = (unsigned short)(value & 0xFFFF);
	}
}

int BuzzParamLayout::ReadGlobalParam(const void* globalVals, int slotIndex) const
{
	if (!globalVals || slotIndex < 0 || slotIndex >= (int)globalSlots.size())
		return 0;

	const ParamSlot& slot = globalSlots[slotIndex];
	const unsigned char* ptr = (const unsigned char*)globalVals + slot.byteOffset;

	if (slot.byteSize == 1) {
		return *ptr;
	} else if (slot.byteSize == 2) {
		return *(const unsigned short*)ptr;
	}
	return 0;
}

void BuzzParamLayout::WriteAllNoValues(void* globalVals) const
{
	if (!globalVals) return;

	for (int i = 0; i < (int)globalSlots.size(); i++) {
		WriteGlobalParam(globalVals, i, globalSlots[i].param->NoValue);
	}
}

void BuzzParamLayout::WriteAllDefaults(void* globalVals) const
{
	if (!globalVals) return;

	for (int i = 0; i < (int)globalSlots.size(); i++) {
		const ParamSlot& slot = globalSlots[i];
		if (slot.param->Flags & MPF_STATE) {
			WriteGlobalParam(globalVals, i, slot.param->DefValue);
		} else {
			WriteGlobalParam(globalVals, i, slot.param->NoValue);
		}
	}
}

void BuzzParamLayout::WriteTrackParam(void* trackVals, int trackIndex, int slotIndex, int value) const
{
	if (!trackVals || slotIndex < 0 || slotIndex >= (int)trackSlots.size())
		return;
	if (trackStructSize <= 0 || trackIndex < 0 || trackIndex > INT_MAX / trackStructSize)
		return;

	const ParamSlot& slot = trackSlots[slotIndex];
	unsigned char* basePtr = (unsigned char*)trackVals + trackIndex * trackStructSize;
	unsigned char* ptr = basePtr + slot.byteOffset;

	if (slot.byteSize == 1) {
		*ptr = (unsigned char)(value & 0xFF);
	} else if (slot.byteSize == 2) {
		*(unsigned short*)ptr = (unsigned short)(value & 0xFFFF);
	}
}

int BuzzParamLayout::ReadTrackParam(const void* trackVals, int trackIndex, int slotIndex) const
{
	if (!trackVals || slotIndex < 0 || slotIndex >= (int)trackSlots.size())
		return 0;
	if (trackStructSize <= 0 || trackIndex < 0 || trackIndex > INT_MAX / trackStructSize)
		return 0;

	const ParamSlot& slot = trackSlots[slotIndex];
	const unsigned char* basePtr = (const unsigned char*)trackVals + trackIndex * trackStructSize;
	const unsigned char* ptr = basePtr + slot.byteOffset;

	if (slot.byteSize == 1) {
		return *ptr;
	} else if (slot.byteSize == 2) {
		return *(const unsigned short*)ptr;
	}
	return 0;
}

void BuzzParamLayout::WriteTrackAllNoValues(void* trackVals, int numTracks) const
{
	if (!trackVals) return;

	for (int t = 0; t < numTracks; t++) {
		for (int i = 0; i < (int)trackSlots.size(); i++) {
			WriteTrackParam(trackVals, t, i, trackSlots[i].param->NoValue);
		}
	}
}

} // namespace BuzzVst
