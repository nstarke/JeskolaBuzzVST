#include "BuzzOscTables.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <atomic>

namespace BuzzVst {

static std::atomic<bool> sOscInitialized{false};
static bool sOscGenerating = false; // only accessed under the init check
short BuzzOscTables::tables[BUZZ_NUM_WAVEFORMS][BUZZ_OSC_TABLE_TOTAL_SAMPLES];

// Keep the old static for header compatibility
bool BuzzOscTables::initialized = false;

static const double TWO_PI = 6.283185307179586476925286766559;

void BuzzOscTables::Initialize()
{
	// Fast path: already initialized (atomic load, no lock needed)
	if (sOscInitialized.load(std::memory_order_acquire)) return;

	// Slow path: generate tables (benign race — worst case we generate twice)
	if (sOscGenerating) return; // prevent reentrant call
	sOscGenerating = true;

	memset(tables, 0, sizeof(tables));

	GenerateSine();
	GenerateSawtooth();
	GeneratePulse();
	GenerateTriangle();
	GenerateNoise();
	Generate303Sawtooth();

	initialized = true;
	sOscGenerating = false;
	sOscInitialized.store(true, std::memory_order_release);
}

const short* BuzzOscTables::GetTable(int waveform)
{
	if (!initialized) Initialize();
	if (waveform < 0 || waveform >= BUZZ_NUM_WAVEFORMS) return nullptr;
	return tables[waveform];
}

void BuzzOscTables::GenerateSine()
{
	for (int level = 0; level <= 10; level++) {
		int numSamples = 2048 >> level;
		int offset = GetOscTblOffset(level);

		for (int i = 0; i < numSamples; i++) {
			double phase = TWO_PI * (double)i / (double)numSamples;
			tables[OWF_SINE][offset + i] = (short)(sin(phase) * 32767.0);
		}
	}
}

void BuzzOscTables::GenerateSawtooth()
{
	// Bandlimited sawtooth using additive synthesis
	for (int level = 0; level <= 10; level++) {
		int numSamples = 2048 >> level;
		int offset = GetOscTblOffset(level);
		int maxHarmonics = numSamples / 2;

		for (int i = 0; i < numSamples; i++) {
			double phase = TWO_PI * (double)i / (double)numSamples;
			double val = 0.0;

			for (int h = 1; h <= maxHarmonics; h++) {
				val += sin(phase * h) / (double)h;
			}
			val *= 2.0 / BUZZ_PI;
			tables[OWF_SAWTOOTH][offset + i] = (short)(val * 32767.0);
		}
	}
}

void BuzzOscTables::GeneratePulse()
{
	// Bandlimited square wave (odd harmonics only)
	for (int level = 0; level <= 10; level++) {
		int numSamples = 2048 >> level;
		int offset = GetOscTblOffset(level);
		int maxHarmonics = numSamples / 2;

		for (int i = 0; i < numSamples; i++) {
			double phase = TWO_PI * (double)i / (double)numSamples;
			double val = 0.0;

			for (int h = 1; h <= maxHarmonics; h += 2) {
				val += sin(phase * h) / (double)h;
			}
			val *= 4.0 / BUZZ_PI;
			tables[OWF_PULSE][offset + i] = (short)(val * 32767.0);
		}
	}
}

void BuzzOscTables::GenerateTriangle()
{
	// Bandlimited triangle (odd harmonics, alternating sign, 1/h^2)
	for (int level = 0; level <= 10; level++) {
		int numSamples = 2048 >> level;
		int offset = GetOscTblOffset(level);
		int maxHarmonics = numSamples / 2;

		for (int i = 0; i < numSamples; i++) {
			double phase = TWO_PI * (double)i / (double)numSamples;
			double val = 0.0;
			double sign = 1.0;

			for (int h = 1; h <= maxHarmonics; h += 2) {
				val += sign * sin(phase * h) / ((double)h * (double)h);
				sign = -sign;
			}
			val *= 8.0 / (BUZZ_PI * BUZZ_PI);
			tables[OWF_TRIANGLE][offset + i] = (short)(val * 32767.0);
		}
	}
}

void BuzzOscTables::GenerateNoise()
{
	srand(12345); // deterministic seed
	for (int level = 0; level <= 10; level++) {
		int numSamples = 2048 >> level;
		int offset = GetOscTblOffset(level);

		for (int i = 0; i < numSamples; i++) {
			tables[OWF_NOISE][offset + i] = (short)((rand() % 65536) - 32768);
		}
	}
}

void BuzzOscTables::Generate303Sawtooth()
{
	// Similar to regular sawtooth but with slight harmonic modifications
	// to emulate the TB-303's characteristic waveform
	for (int level = 0; level <= 10; level++) {
		int numSamples = 2048 >> level;
		int offset = GetOscTblOffset(level);
		int maxHarmonics = numSamples / 2;

		for (int i = 0; i < numSamples; i++) {
			double phase = TWO_PI * (double)i / (double)numSamples;
			double val = 0.0;

			for (int h = 1; h <= maxHarmonics; h++) {
				// Slightly damped higher harmonics for 303 character
				double damping = 1.0 / (1.0 + 0.01 * h);
				val += damping * sin(phase * h) / (double)h;
			}
			val *= 2.0 / BUZZ_PI;
			tables[OWF_303_SAWTOOTH][offset + i] = (short)(val * 32767.0);
		}
	}
}

} // namespace BuzzVst
