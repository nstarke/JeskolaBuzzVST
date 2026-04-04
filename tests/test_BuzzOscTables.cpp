#include <windows.h>
#include "TestFramework.h"
#include "../src/buzz/MachineInterface.h"
#include "../src/buzz/BuzzOscTables.h"

using namespace BuzzVst;

// ===== Initialization =====

TEST(OscTables, InitializationDoesNotCrash) {
	BuzzOscTables::Initialize();
	ASSERT_TRUE(true);
}

// ===== GetTable returns valid data =====

TEST(OscTables, GetSineTable) {
	BuzzOscTables::Initialize();
	const short* table = BuzzOscTables::GetTable(OWF_SINE);
	ASSERT_NOT_NULL(table);
}

TEST(OscTables, GetSawtoothTable) {
	const short* table = BuzzOscTables::GetTable(OWF_SAWTOOTH);
	ASSERT_NOT_NULL(table);
}

TEST(OscTables, GetPulseTable) {
	const short* table = BuzzOscTables::GetTable(OWF_PULSE);
	ASSERT_NOT_NULL(table);
}

TEST(OscTables, GetTriangleTable) {
	const short* table = BuzzOscTables::GetTable(OWF_TRIANGLE);
	ASSERT_NOT_NULL(table);
}

TEST(OscTables, GetNoiseTable) {
	const short* table = BuzzOscTables::GetTable(OWF_NOISE);
	ASSERT_NOT_NULL(table);
}

TEST(OscTables, Get303SawtoothTable) {
	const short* table = BuzzOscTables::GetTable(OWF_303_SAWTOOTH);
	ASSERT_NOT_NULL(table);
}

TEST(OscTables, InvalidWaveformReturnsNull) {
	ASSERT_NULL(BuzzOscTables::GetTable(-1));
	ASSERT_NULL(BuzzOscTables::GetTable(10));
	ASSERT_NULL(BuzzOscTables::GetTable(999));
}

// ===== GetOscTblOffset =====

TEST(OscTables, OffsetLevel0) {
	int offset = GetOscTblOffset(0);
	ASSERT_EQ(offset, 0);
}

TEST(OscTables, OffsetLevel1) {
	int offset = GetOscTblOffset(1);
	ASSERT_EQ(offset, 2048);
}

TEST(OscTables, OffsetLevel2) {
	int offset = GetOscTblOffset(2);
	ASSERT_EQ(offset, 2048 + 1024);
}

TEST(OscTables, OffsetsMonotonicallyIncrease) {
	int prev = -1;
	for (int level = 0; level <= 10; level++) {
		int offset = GetOscTblOffset(level);
		ASSERT_GT(offset, prev);
		prev = offset;
	}
}

// ===== Sine waveform properties =====

TEST(OscTables, SineStartsAtZero) {
	const short* sine = BuzzOscTables::GetTable(OWF_SINE);
	// First sample of level 0 should be near zero (sin(0) = 0)
	ASSERT_LT(abs(sine[0]), 100); // allow small error
}

TEST(OscTables, SineQuarterWaveIsPeak) {
	const short* sine = BuzzOscTables::GetTable(OWF_SINE);
	// At 1/4 of 2048 samples = sample 512, should be near +32767
	short peak = sine[512];
	ASSERT_GT(peak, 32000);
}

TEST(OscTables, SineHalfWaveReturnsToZero) {
	const short* sine = BuzzOscTables::GetTable(OWF_SINE);
	// At 1/2 of 2048 samples = sample 1024, should be near zero
	ASSERT_LT(abs(sine[1024]), 100);
}

TEST(OscTables, SineThreeQuarterIsNegativePeak) {
	const short* sine = BuzzOscTables::GetTable(OWF_SINE);
	// At 3/4 of 2048 = sample 1536, should be near -32767
	ASSERT_LT(sine[1536], -32000);
}

TEST(OscTables, SineIsNormalized) {
	const short* sine = BuzzOscTables::GetTable(OWF_SINE);
	short maxVal = 0;
	for (int i = 0; i < 2048; i++) {
		short absVal = (short)abs(sine[i]);
		if (absVal > maxVal) maxVal = absVal;
	}
	// Peak should be close to 32767
	ASSERT_GT(maxVal, 32700);
	ASSERT_LE(maxVal, 32767);
}

// ===== All waveforms have signal (not all zeros) =====

TEST(OscTables, AllWaveformsHaveSignal) {
	for (int wf = 0; wf < 6; wf++) {
		const short* table = BuzzOscTables::GetTable(wf);
		ASSERT_NOT_NULL(table);

		bool hasNonZero = false;
		for (int i = 0; i < 2048; i++) {
			if (table[i] != 0) {
				hasNonZero = true;
				break;
			}
		}
		CHECK_TRUE(hasNonZero);
	}
}

// ===== Multi-level consistency =====

TEST(OscTables, AllLevelsHaveSignal) {
	const short* sine = BuzzOscTables::GetTable(OWF_SINE);

	for (int level = 0; level <= 10; level++) {
		int offset = GetOscTblOffset(level);
		int numSamples = 2048 >> level;
		bool hasNonZero = false;

		for (int i = 0; i < numSamples; i++) {
			if (sine[offset + i] != 0) {
				hasNonZero = true;
				break;
			}
		}
		CHECK_TRUE(hasNonZero);
	}
}

// ===== Noise is not silence and has variation =====

TEST(OscTables, NoiseHasVariation) {
	const short* noise = BuzzOscTables::GetTable(OWF_NOISE);
	short minVal = 32767, maxVal = -32768;

	for (int i = 0; i < 2048; i++) {
		if (noise[i] < minVal) minVal = noise[i];
		if (noise[i] > maxVal) maxVal = noise[i];
	}

	// Noise should span a significant range
	int range = maxVal - minVal;
	ASSERT_GT(range, 30000);
}

// ===== Sawtooth has both positive and negative values =====

TEST(OscTables, SawtoothBipolar) {
	const short* saw = BuzzOscTables::GetTable(OWF_SAWTOOTH);
	bool hasPositive = false, hasNegative = false;

	for (int i = 0; i < 2048; i++) {
		if (saw[i] > 1000) hasPositive = true;
		if (saw[i] < -1000) hasNegative = true;
	}

	ASSERT_TRUE(hasPositive);
	ASSERT_TRUE(hasNegative);
}
