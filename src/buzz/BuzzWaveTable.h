#pragma once

#include "MachineInterface.h"
#include <string>
#include <vector>

namespace BuzzVst {

// Maximum number of wave slots (Buzz uses 1-based indices, 1..200)
static const int kMaxWaveSlots = 200;

struct WaveSlot {
	bool loaded = false;
	std::string name;               // Display name
	std::string filePath;           // Original file path (for persistence)
	CWaveInfo info = {};            // Flags, Volume
	CWaveLevel level = {};          // numSamples, pSamples, RootNote, SamplesPerSec, LoopStart, LoopEnd
	std::vector<short> samples;     // Owned sample data (level.pSamples points into this)
};

// Stores up to 200 wave slots that Buzz machines can access via
// the CMICallbacks wave functions. Handles WAV file loading and
// conversion to 16-bit signed PCM.
class BuzzWaveTable {
public:
	BuzzWaveTable();
	~BuzzWaveTable();

	// Load a WAV file into a specific slot (1-based index).
	// Returns true on success.
	bool LoadWav(int slotIndex, const std::string& wavPath);

	// Load a WAV file into the next available slot.
	// Returns the slot index (1-based), or 0 if no slots available.
	int LoadWavAuto(const std::string& wavPath);

	// Clear a specific slot
	void Clear(int slotIndex);

	// Clear all slots
	void ClearAll();

	// Buzz callback implementations
	const CWaveInfo* GetWave(int i) const;
	const CWaveLevel* GetWaveLevel(int i, int level) const;
	const CWaveLevel* GetNearestWaveLevel(int i, int note) const;
	const char* GetWaveName(int i) const;
	int GetFreeWave() const;

	// Query
	bool IsLoaded(int i) const;
	int GetLoadedCount() const;
	const WaveSlot* GetSlot(int i) const;

	// Get all loaded file paths (for state persistence)
	struct WavePathEntry {
		int slotIndex;
		std::string filePath;
	};
	std::vector<WavePathEntry> GetLoadedPaths() const;

private:
	WaveSlot slots[kMaxWaveSlots]; // 0-indexed internally, but API is 1-based

	bool parseWavFile(const std::string& path, std::vector<short>& outSamples,
	                  int& outSampleRate, int& outChannels);
};

} // namespace BuzzVst
