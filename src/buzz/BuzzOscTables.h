#pragma once

#include "MachineInterface.h"

namespace BuzzVst {

// Buzz oscillator tables: 11 levels of bandlimited waveforms.
// Level 0 = 2048 samples, level 1 = 1024, ... level 10 = 4 samples.
// 16-bit signed integers, one full cycle per level.
// Total samples per waveform: 2048+1024+512+256+128+64+32+16+8+4 = 4092

#define BUZZ_OSC_TABLE_TOTAL_SAMPLES 4092
#define BUZZ_NUM_WAVEFORMS 6

class BuzzOscTables {
public:
	static void Initialize();
	static const short* GetTable(int waveform);

private:
	static bool initialized;
	static short tables[BUZZ_NUM_WAVEFORMS][BUZZ_OSC_TABLE_TOTAL_SAMPLES];

	static void GenerateSine();
	static void GenerateSawtooth();
	static void GeneratePulse();
	static void GenerateTriangle();
	static void GenerateNoise();
	static void Generate303Sawtooth();

	static void GenerateWaveformLevel(int waveform, int level, int numSamples);
};

} // namespace BuzzVst
