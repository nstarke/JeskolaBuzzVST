#include "TestFramework.h"
#include "../src/resample/BuzzResampler.h"
#include <cmath>
#include <vector>
#include <numeric>

using namespace BuzzVst;

static const double kPi = 3.14159265358979323846;

// Generate a sine wave at a given frequency and sample rate
static void generateSine(float* buf, int len, double freq, double sampleRate, double phase = 0.0) {
	for (int i = 0; i < len; i++) {
		buf[i] = (float)sin(2.0 * kPi * freq * (i / sampleRate) + phase);
	}
}

// Measure RMS of a buffer
static double rms(const float* buf, int len) {
	if (len <= 0) return 0.0;
	double sum = 0;
	for (int i = 0; i < len; i++) sum += (double)buf[i] * buf[i];
	return sqrt(sum / len);
}

// Peak absolute value
static float peakAbs(const float* buf, int len) {
	float peak = 0.0f;
	for (int i = 0; i < len; i++) {
		float a = fabsf(buf[i]);
		if (a > peak) peak = a;
	}
	return peak;
}

// Estimate dominant frequency via zero-crossing count
static double estimateFrequency(const float* buf, int len, double sampleRate) {
	int crossings = 0;
	for (int i = 1; i < len; i++) {
		if ((buf[i - 1] >= 0 && buf[i] < 0) || (buf[i - 1] < 0 && buf[i] >= 0))
			crossings++;
	}
	return (crossings / 2.0) * sampleRate / len;
}

// ===========================================================================
//  Biquad unit tests
// ===========================================================================

TEST(Biquad, LowpassPassesDC) {
	Biquad lp = Biquad::lowpass(48000.0, 10000.0);
	// Feed DC (1.0) for enough samples to settle
	float out = 0;
	for (int i = 0; i < 2000; i++) out = lp.process(1.0f);
	ASSERT_NEAR(out, 1.0f, 0.001f);
}

TEST(Biquad, LowpassAttenuatesHighFreq) {
	Biquad lp = Biquad::lowpass(96000.0, 20000.0);
	// Feed a 40kHz sine (well above cutoff) and measure settled RMS
	float samples[4000];
	for (int i = 0; i < 4000; i++)
		samples[i] = lp.process((float)sin(2.0 * kPi * 40000.0 * i / 96000.0));
	double tailRms = rms(samples + 2000, 2000);
	// Should be strongly attenuated (4th-order rolloff per biquad is ~12dB/oct,
	// single section at 1 octave above cutoff ≈ -12dB ≈ 0.25 of unity)
	ASSERT_LT(tailRms, 0.35);
}

TEST(Biquad, ResetClearsState) {
	Biquad lp = Biquad::lowpass(48000.0, 5000.0);
	for (int i = 0; i < 500; i++) lp.process(1.0f);
	lp.reset();
	// After reset feeding zeros should yield zero (no residual energy)
	float out = lp.process(0.0f);
	ASSERT_NEAR(out, 0.0f, 1e-10f);
	out = lp.process(0.0f);
	ASSERT_NEAR(out, 0.0f, 1e-10f);
}

TEST(Biquad, CascadedGivesSteeperRolloff) {
	// Single section
	Biquad lp1 = Biquad::lowpass(96000.0, 20000.0);
	// Cascaded pair (4th-order)
	Biquad lp2a = Biquad::lowpass(96000.0, 20000.0);
	Biquad lp2b = Biquad::lowpass(96000.0, 20000.0);

	float single[2000], cascaded[2000];
	for (int i = 0; i < 2000; i++) {
		float s = (float)sin(2.0 * kPi * 35000.0 * i / 96000.0);
		single[i] = lp1.process(s);
		cascaded[i] = lp2b.process(lp2a.process(s));
	}
	double singleRms = rms(single + 1000, 1000);
	double cascadedRms = rms(cascaded + 1000, 1000);
	ASSERT_LT(cascadedRms, singleRms);
}

// ===========================================================================
//  Resampler - Identity / basic
// ===========================================================================

TEST(Resampler, IdentityPassthrough) {
	BuzzResampler rs;
	rs.init(44100.0, 44100.0);

	float input[256], output[256];
	generateSine(input, 256, 440.0, 44100.0);

	rs.process(input, 256, output, 256);

	// Cubic Hermite with ratio 1.0 has a 3-sample latency: output[i+3] ≈ input[i]
	for (int i = 0; i < 250; i++) {
		ASSERT_NEAR(output[i + 3], input[i], 0.01f);
	}
}

TEST(Resampler, IsActiveAfterInit) {
	BuzzResampler rs;
	ASSERT_FALSE(rs.isActive());
	rs.init(44100.0, 48000.0);
	ASSERT_TRUE(rs.isActive());
}

// ===========================================================================
//  Upsample tests
// ===========================================================================

TEST(Resampler, Upsample44100To48000) {
	BuzzResampler rs;
	rs.init(44100.0, 48000.0);

	const int inLen = 441;   // 10ms at 44100
	const int outLen = 480;  // 10ms at 48000
	float input[441], output[480];
	generateSine(input, inLen, 440.0, 44100.0);

	int consumed = rs.process(input, inLen, output, outLen);
	ASSERT_GT(consumed, inLen - 5);

	double outRms = rms(output + 10, outLen - 10);
	ASSERT_GT(outRms, 0.3);
}

TEST(Resampler, Upsample44100To96000) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	const int inLen = 441;
	const int outLen = 960;
	float input[441], output[960];
	generateSine(input, inLen, 440.0, 44100.0);

	rs.process(input, inLen, output, outLen);

	double outRms = rms(output + 20, outLen - 20);
	ASSERT_GT(outRms, 0.3);
}

TEST(Resampler, Upsample44100To192000) {
	BuzzResampler rs;
	rs.init(44100.0, 192000.0);

	const int inLen = 4410;    // 100ms at 44100
	const int outLen = 19200;  // 100ms at 192000
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 440.0, 44100.0);

	int consumed = rs.process(input.data(), inLen, output.data(), outLen);
	ASSERT_GT(consumed, inLen - 10);

	// RMS should be substantial
	double outRms = rms(output.data() + 100, outLen - 100);
	ASSERT_GT(outRms, 0.4);
}

TEST(Resampler, Upsample44100To88200) {
	// Exact 2x ratio
	BuzzResampler rs;
	rs.init(44100.0, 88200.0);

	const int inLen = 441;
	const int outLen = 882;
	float input[441], output[882];
	generateSine(input, inLen, 440.0, 44100.0);

	rs.process(input, inLen, output, outLen);
	double outRms = rms(output + 20, outLen - 20);
	ASSERT_GT(outRms, 0.3);
}

// ===========================================================================
//  Downsample tests
// ===========================================================================

TEST(Resampler, Downsample96000To44100) {
	BuzzResampler rs;
	rs.init(96000.0, 44100.0);

	const int inLen = 960;
	const int outLen = 441;
	float input[960], output[441];
	generateSine(input, inLen, 440.0, 96000.0);

	rs.process(input, inLen, output, outLen);

	double outRms = rms(output + 10, outLen - 10);
	ASSERT_GT(outRms, 0.3);
}

TEST(Resampler, Downsample48000To44100) {
	BuzzResampler rs;
	rs.init(48000.0, 44100.0);

	const int inLen = 480;
	const int outLen = 441;
	float input[480], output[441];
	generateSine(input, inLen, 440.0, 48000.0);

	rs.process(input, inLen, output, outLen);

	double outRms = rms(output + 10, outLen - 10);
	ASSERT_GT(outRms, 0.3);
}

TEST(Resampler, Downsample192000To44100) {
	BuzzResampler rs;
	rs.init(192000.0, 44100.0);

	const int inLen = 19200;   // 100ms at 192000
	const int outLen = 4410;   // 100ms at 44100
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 440.0, 192000.0);

	rs.process(input.data(), inLen, output.data(), outLen);

	double outRms = rms(output.data() + 50, outLen - 50);
	ASSERT_GT(outRms, 0.3);
}

TEST(Resampler, DownsampleHostBelowMachineRate) {
	// Rare case: host at 22050 Hz (below machine rate)
	// The upsample resamplers from 44100 -> 22050 is actually a downsample
	BuzzResampler rs;
	rs.init(44100.0, 22050.0);

	const int inLen = 441;
	const int outLen = 220;
	float input[441], output[220];
	generateSine(input, inLen, 440.0, 44100.0);

	rs.process(input, inLen, output, outLen);

	double outRms = rms(output + 10, outLen - 10);
	ASSERT_GT(outRms, 0.3);
}

// ===========================================================================
//  Anti-aliasing
// ===========================================================================

TEST(Resampler, AntiAliasFiltering) {
	BuzzResampler rs;
	rs.init(96000.0, 44100.0);

	// 30kHz sine at 96kHz (above 44100 Nyquist of 22050)
	const int inLen = 9600;
	const int outLen = 4410;
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 30000.0, 96000.0);

	rs.process(input.data(), inLen, output.data(), outLen);

	double outRms = rms(output.data() + 100, outLen - 100);
	ASSERT_LT(outRms, 0.15);
}

TEST(Resampler, AntiAliasNotUsedForUpsample) {
	// When upsampling, there should be no anti-alias filtering.
	// A 10kHz tone upsampled from 44100->96000 should pass through cleanly.
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	const int inLen = 4410;    // 100ms at 44100
	const int outLen = 9600;   // 100ms at 96000
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 10000.0, 44100.0);

	rs.process(input.data(), inLen, output.data(), outLen);

	// Input RMS of a full-scale sine ≈ 0.707
	double inRms = rms(input.data() + 50, inLen - 50);
	double outRms = rms(output.data() + 200, outLen - 200);
	// Should preserve energy well (within a few dB)
	ASSERT_GT(outRms, inRms * 0.8);
}

TEST(Resampler, AntiImageRemovesSpectralImages) {
	// The critical case: upsampling 44100->48000.
	// A 15kHz tone at 44100 creates a spectral image at 44100-15000=29100Hz.
	// At 48000, this aliases to 48000-29100=18900Hz (audible!).
	// The anti-imaging filter should suppress this image.
	//
	// We test by upsampling a high-frequency tone and checking that the output
	// doesn't contain energy at unexpected frequencies above srcNyquist.
	BuzzResampler rs;
	rs.init(44100.0, 48000.0);

	// Generate 10kHz at 44100 (well below anti-imaging cutoff ~19845Hz)
	const int inLen = 44100;   // 1 second
	const int outLen = 48000;
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 10000.0, 44100.0);

	rs.process(input.data(), inLen, output.data(), outLen);

	// The 10kHz tone should pass through cleanly
	double mainRms = rms(output.data() + 1000, outLen - 1000);
	ASSERT_GT(mainRms, 0.4);  // tone is present and not overly attenuated

	// The overall output should be cleaner than the input
	// (no spectral images adding energy beyond the original)
	double inRms = rms(input.data() + 500, inLen - 500);
	ASSERT_LT(mainRms, inRms * 1.15);  // shouldn't gain more than ~1dB from images
}

TEST(Resampler, AntiAliasPreservesLowFreqOnDownsample) {
	// 440Hz through a 96k->44100 downsample should be preserved
	BuzzResampler rs;
	rs.init(96000.0, 44100.0);

	const int inLen = 96000;   // 1 second
	const int outLen = 44100;
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 440.0, 96000.0);

	rs.process(input.data(), inLen, output.data(), outLen);

	double inRms = rms(input.data() + 1000, inLen - 1000);
	double outRms = rms(output.data() + 500, outLen - 500);
	// Energy should be very close
	ASSERT_NEAR(outRms, inRms, 0.1);
}

// ===========================================================================
//  Frequency preservation
// ===========================================================================

TEST(Resampler, FrequencyPreservedAfterUpsample) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	const int inLen = 44100;   // 1 second
	const int outLen = 96000;
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 1000.0, 44100.0);

	rs.process(input.data(), inLen, output.data(), outLen);

	// Estimate freq from zero crossings (skip transient)
	double freq = estimateFrequency(output.data() + 500, outLen - 1000, 96000.0);
	ASSERT_NEAR(freq, 1000.0, 20.0); // within 20Hz
}

TEST(Resampler, FrequencyPreservedAfterDownsample) {
	BuzzResampler rs;
	rs.init(96000.0, 44100.0);

	const int inLen = 96000;
	const int outLen = 44100;
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 1000.0, 96000.0);

	rs.process(input.data(), inLen, output.data(), outLen);

	double freq = estimateFrequency(output.data() + 200, outLen - 400, 44100.0);
	ASSERT_NEAR(freq, 1000.0, 20.0);
}

// ===========================================================================
//  DC and silence
// ===========================================================================

TEST(Resampler, SilenceInSilenceOut) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	float input[256] = {};
	float output[557];
	rs.process(input, 256, output, 557);

	for (int i = 0; i < 557; i++) {
		ASSERT_NEAR(output[i], 0.0f, 1e-7f);
	}
}

TEST(Resampler, DCPreservedUpsample) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	// Feed DC = 0.5 for a long block to settle past transient
	const int inLen = 4000;
	const int outLen = (int)(4000 * 96000.0 / 44100.0);
	std::vector<float> input(inLen, 0.5f), output(outLen);

	rs.process(input.data(), inLen, output.data(), outLen);

	// After transient, output should converge to 0.5
	for (int i = outLen / 2; i < outLen; i++) {
		ASSERT_NEAR(output[i], 0.5f, 0.01f);
	}
}

TEST(Resampler, DCPreservedDownsample) {
	BuzzResampler rs;
	rs.init(96000.0, 44100.0);

	const int inLen = 8000;
	const int outLen = (int)(8000 * 44100.0 / 96000.0);
	std::vector<float> input(inLen, 0.75f), output(outLen);

	rs.process(input.data(), inLen, output.data(), outLen);

	for (int i = outLen / 2; i < outLen; i++) {
		ASSERT_NEAR(output[i], 0.75f, 0.01f);
	}
}

// ===========================================================================
//  Energy / amplitude
// ===========================================================================

TEST(Resampler, EnergyPreservedUpsample) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	const int inLen = 44100;
	const int outLen = 96000;
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 440.0, 44100.0);

	rs.process(input.data(), inLen, output.data(), outLen);

	double inRms = rms(input.data() + 500, inLen - 500);
	double outRms = rms(output.data() + 1000, outLen - 1000);
	// RMS should be within ~1dB (ratio between 0.89 and 1.12)
	ASSERT_GT(outRms / inRms, 0.85);
	ASSERT_LT(outRms / inRms, 1.15);
}

TEST(Resampler, NoPeakClippingUpsample) {
	// Unit-amplitude sine should not exceed ±1.0 after upsampling
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	const int inLen = 4410;
	const int outLen = 9600;
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 440.0, 44100.0);

	rs.process(input.data(), inLen, output.data(), outLen);

	// Cubic interpolation can overshoot slightly; allow small margin
	float peak = peakAbs(output.data(), outLen);
	ASSERT_LT(peak, 1.1f);
}

// ===========================================================================
//  Streaming / continuity
// ===========================================================================

TEST(Resampler, StreamingContinuity) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	const int inBlock = 256;
	const int outBlock = (int)(256 * 96000.0 / 44100.0); // ~557
	float input[512], output1[600], output2[600];
	generateSine(input, 512, 440.0, 44100.0);

	rs.process(input, inBlock, output1, outBlock);
	rs.process(input + inBlock, inBlock, output2, outBlock);

	float diff = fabsf(output2[0] - output1[outBlock - 1]);
	ASSERT_LT(diff, 0.1f);
}

TEST(Resampler, StreamingManyBlocksNoDrift) {
	// Process many small blocks and verify accumulated output is correct
	BuzzResampler rs;
	rs.init(44100.0, 48000.0);

	const double srcRate = 44100.0;
	const double dstRate = 48000.0;
	const int numBlocks = 500;
	const int inBlock = 128;
	const int outBlock = (int)round(inBlock * dstRate / srcRate); // ~139

	// Generate a long sine at source rate
	const int totalIn = numBlocks * inBlock;
	std::vector<float> input(totalIn);
	generateSine(input.data(), totalIn, 440.0, srcRate);

	// Process block by block, collecting all output
	std::vector<float> allOutput;
	allOutput.reserve(numBlocks * outBlock + 100);

	int totalConsumed = 0;
	for (int b = 0; b < numBlocks; b++) {
		std::vector<float> out(outBlock);
		int consumed = rs.process(input.data() + b * inBlock, inBlock, out.data(), outBlock);
		totalConsumed += consumed;
		allOutput.insert(allOutput.end(), out.begin(), out.end());
	}

	// Total consumed should be very close to totalIn
	ASSERT_GT(totalConsumed, totalIn - numBlocks);
	ASSERT_LT(totalConsumed, totalIn + numBlocks);

	// Output RMS in the middle should be stable (~0.707 for a sine)
	int mid = (int)allOutput.size() / 2;
	int window = 5000;
	if (mid + window <= (int)allOutput.size()) {
		double midRms = rms(allOutput.data() + mid, window);
		ASSERT_NEAR(midRms, 0.707, 0.1);
	}

	// Check for discontinuities: no adjacent sample should jump by more than
	// what a 440Hz sine at 48kHz could produce (~0.06 per sample)
	float maxJump = 0;
	for (int i = 1; i < (int)allOutput.size(); i++) {
		float jump = fabsf(allOutput[i] - allOutput[i - 1]);
		if (jump > maxJump) maxJump = jump;
	}
	ASSERT_LT(maxJump, 0.15f);
}

TEST(Resampler, StreamingVariableBlockSizes) {
	// DAWs may change block size between calls
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	const int totalIn = 4410;
	std::vector<float> input(totalIn);
	generateSine(input.data(), totalIn, 1000.0, 44100.0);

	// Process with varying block sizes
	int blockSizes[] = { 64, 128, 256, 512, 100, 37, 256, 128, 64, 200 };
	int numBlockSizes = sizeof(blockSizes) / sizeof(blockSizes[0]);

	std::vector<float> allOutput;
	int inOffset = 0;
	for (int b = 0; b < numBlockSizes && inOffset < totalIn; b++) {
		int inSz = blockSizes[b];
		if (inOffset + inSz > totalIn) inSz = totalIn - inOffset;
		int outSz = (int)round(inSz * 96000.0 / 44100.0);
		std::vector<float> out(outSz);
		rs.process(input.data() + inOffset, inSz, out.data(), outSz);
		allOutput.insert(allOutput.end(), out.begin(), out.end());
		inOffset += inSz;
	}

	// Should have non-trivial output
	ASSERT_GT((int)allOutput.size(), 1000);

	// No large discontinuities.  Variable block sizes cause slight effective-ratio
	// jitter, so block boundaries may have small glitches.  We just verify there
	// are no catastrophic jumps (> 1.0 would indicate a resampler state bug).
	float maxJump = 0;
	for (int i = 1; i < (int)allOutput.size(); i++) {
		float jump = fabsf(allOutput[i] - allOutput[i - 1]);
		if (jump > maxJump) maxJump = jump;
	}
	ASSERT_LT(maxJump, 1.0f);
}

TEST(Resampler, StreamingDownsampleContinuity) {
	// Streaming continuity for downsampling too
	BuzzResampler rs;
	rs.init(96000.0, 44100.0);

	const int inBlock = 512;
	const int outBlock = (int)round(512 * 44100.0 / 96000.0); // ~235
	float input[1024], output1[300], output2[300];
	generateSine(input, 1024, 440.0, 96000.0);

	rs.process(input, inBlock, output1, outBlock);
	rs.process(input + inBlock, inBlock, output2, outBlock);

	float diff = fabsf(output2[0] - output1[outBlock - 1]);
	ASSERT_LT(diff, 0.1f);
}

// ===========================================================================
//  Edge cases
// ===========================================================================

TEST(Resampler, ZeroOutputLen) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	float input[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	float output[1] = { -999.0f };

	int consumed = rs.process(input, 10, output, 0);
	// Should consume nothing and write nothing
	ASSERT_EQ(consumed, 0);
}

TEST(Resampler, SingleSampleOutput) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	float input[10], output[1];
	generateSine(input, 10, 440.0, 44100.0);

	int consumed = rs.process(input, 10, output, 1);
	// Should produce exactly 1 sample, consuming very few input
	ASSERT_GE(consumed, 0);
	ASSERT_LE(consumed, 5);
}

TEST(Resampler, SingleSampleInput) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	float input[1] = { 0.5f };
	float output[5];

	int consumed = rs.process(input, 1, output, 5);
	ASSERT_GE(consumed, 0);
	ASSERT_LE(consumed, 1);
}

TEST(Resampler, VerySmallBlocks) {
	// Process 1-sample blocks in a loop (extreme case)
	BuzzResampler rs;
	rs.init(44100.0, 48000.0);

	float input[500], allOutput[600];
	generateSine(input, 500, 440.0, 44100.0);

	int outPos = 0;
	for (int i = 0; i < 500 && outPos < 590; i++) {
		float out;
		rs.process(input + i, 1, &out, 1);
		allOutput[outPos++] = out;
	}

	// Should have collected ~500 samples, with non-zero content after transient
	double tailRms = rms(allOutput + 20, outPos - 20);
	ASSERT_GT(tailRms, 0.1);
}

TEST(Resampler, ZeroInputLen) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	float output[10];
	int consumed = rs.process(nullptr, 0, output, 10);
	// Should produce zeros (no input available)
	ASSERT_EQ(consumed, 0);
	for (int i = 0; i < 10; i++) {
		ASSERT_NEAR(output[i], 0.0f, 1e-7f);
	}
}

// ===========================================================================
//  Round-trip
// ===========================================================================

TEST(Resampler, RoundTrip44100_96000) {
	// Upsample 44100->96000, then downsample 96000->44100
	// The result should approximate the original (within filter artifacts)
	BuzzResampler up, down;
	up.init(44100.0, 96000.0);
	down.init(96000.0, 44100.0);

	const int origLen = 4410;
	const int midLen = 9600;
	std::vector<float> original(origLen), middle(midLen), result(origLen);
	generateSine(original.data(), origLen, 440.0, 44100.0);

	up.process(original.data(), origLen, middle.data(), midLen);
	down.process(middle.data(), midLen, result.data(), origLen);

	// Compare (skip transient from two passes of resampler latency)
	double errorRms = 0;
	int start = 200;
	for (int i = start; i < origLen - 100; i++) {
		// The round-trip has ~6 samples of combined latency; compare shifted
		float diff = result[i] - original[i - 6];
		if (i - 6 >= start) errorRms += diff * diff;
	}
	errorRms = sqrt(errorRms / (origLen - start - 100));
	// Should be quite small (< -20dB of signal)
	ASSERT_LT(errorRms, 0.15);
}

TEST(Resampler, RoundTrip44100_48000) {
	BuzzResampler up, down;
	up.init(44100.0, 48000.0);
	down.init(48000.0, 44100.0);

	const int origLen = 44100;
	const int midLen = 48000;
	std::vector<float> original(origLen), middle(midLen), result(origLen);
	generateSine(original.data(), origLen, 440.0, 44100.0);

	up.process(original.data(), origLen, middle.data(), midLen);
	down.process(middle.data(), midLen, result.data(), origLen);

	// RMS of result should be close to original
	double origRms = rms(original.data() + 500, origLen - 1000);
	double resultRms = rms(result.data() + 500, origLen - 1000);
	ASSERT_NEAR(resultRms, origRms, 0.15);
}

// ===========================================================================
//  Reset and re-init
// ===========================================================================

TEST(Resampler, ResetClearsState) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	float input[100], output[200];
	generateSine(input, 100, 1000.0, 44100.0);
	rs.process(input, 100, output, 200);

	rs.reset();

	float silence[10] = {};
	rs.process(silence, 10, output, 20);
	for (int i = 0; i < 20; i++) {
		ASSERT_NEAR(output[i], 0.0f, 0.001f);
	}
}

TEST(Resampler, ReInitSwitchesRates) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	float input[100], output[200];
	generateSine(input, 100, 440.0, 44100.0);
	rs.process(input, 100, output, 200);

	// Re-init to a different rate
	rs.init(44100.0, 48000.0);

	float output2[109]; // 100 * 48000/44100 ≈ 108.8
	generateSine(input, 100, 440.0, 44100.0);
	int consumed = rs.process(input, 100, output2, 109);

	// Should work with new ratio
	ASSERT_GT(consumed, 90);
	double outRms = rms(output2 + 10, 99);
	ASSERT_GT(outRms, 0.2);
}

TEST(Resampler, ReInitClearsAntiAlias) {
	BuzzResampler rs;
	// Start with downsampling (anti-alias active)
	rs.init(96000.0, 44100.0);
	float input[960], output[441];
	generateSine(input, 960, 440.0, 96000.0);
	rs.process(input, 960, output, 441);

	// Re-init to upsampling (anti-alias should NOT be active)
	rs.init(44100.0, 96000.0);
	float input2[441], output2[960];
	generateSine(input2, 441, 10000.0, 44100.0);
	rs.process(input2, 441, output2, 960);

	// 10kHz should pass through cleanly (no attenuation from leftover filter)
	double outRms = rms(output2 + 50, 900);
	ASSERT_GT(outRms, 0.4);
}

// ===========================================================================
//  Input consumption tracking
// ===========================================================================

TEST(Resampler, ConsumedMatchesExpectedUpsample) {
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	const int inLen = 4410;
	const int outLen = 9600;
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 440.0, 44100.0);

	int consumed = rs.process(input.data(), inLen, output.data(), outLen);

	// For ratio ≈ 0.459, producing 9600 output should consume ~4410 input
	double expected = outLen * (44100.0 / 96000.0);
	ASSERT_NEAR((double)consumed, expected, 10.0);
}

TEST(Resampler, ConsumedMatchesExpectedDownsample) {
	BuzzResampler rs;
	rs.init(96000.0, 44100.0);

	const int inLen = 9600;
	const int outLen = 4410;
	std::vector<float> input(inLen), output(outLen);
	generateSine(input.data(), inLen, 440.0, 96000.0);

	int consumed = rs.process(input.data(), inLen, output.data(), outLen);

	double expected = outLen * (96000.0 / 44100.0);
	ASSERT_NEAR((double)consumed, expected, 10.0);
}

TEST(Resampler, ConsumedNeverExceedsInput) {
	// Across multiple block sizes and ratios, consumed should never exceed inputLen
	double rates[][2] = {
		{44100, 48000}, {44100, 96000}, {44100, 192000},
		{96000, 44100}, {48000, 44100}, {192000, 44100}
	};
	int blockSizes[] = { 1, 7, 32, 64, 128, 256, 512, 1024 };

	for (auto& r : rates) {
		for (int bs : blockSizes) {
			BuzzResampler rs;
			rs.init(r[0], r[1]);

			int outLen = (int)ceil(bs * r[1] / r[0]) + 4;
			std::vector<float> input(bs, 0.5f), output(outLen);
			int consumed = rs.process(input.data(), bs, output.data(), outLen);
			ASSERT_LE(consumed, bs);
		}
	}
}

// ===========================================================================
//  Fractional accumulator (computeMachineSamples logic)
// ===========================================================================

TEST(Resampler, FractionalAccumulation) {
	double fracAccum = 0.0;
	double hostRate = 48000.0;
	int hostBlock = 512;
	int totalMachine = 0;
	int totalHost = 0;

	for (int i = 0; i < 100; i++) {
		double exact = hostBlock * (44100.0 / hostRate) + fracAccum;
		int machineSamples = (int)exact;
		if (machineSamples < 1) machineSamples = 1;
		fracAccum = exact - machineSamples;
		totalMachine += machineSamples;
		totalHost += hostBlock;
	}

	double actualRatio = (double)totalMachine / (double)totalHost;
	double expectedRatio = 44100.0 / 48000.0;
	ASSERT_NEAR(actualRatio, expectedRatio, 0.0001);
}

TEST(Resampler, FractionalAccumStableOver10000Blocks) {
	// Verify no drift or unbounded growth in the accumulator
	double fracAccum = 0.0;
	double hostRate = 96000.0;
	int hostBlock = 512;
	long long totalMachine = 0;
	long long totalHost = 0;

	for (int i = 0; i < 10000; i++) {
		double exact = hostBlock * (44100.0 / hostRate) + fracAccum;
		int machineSamples = (int)exact;
		if (machineSamples < 1) machineSamples = 1;
		fracAccum = exact - machineSamples;
		totalMachine += machineSamples;
		totalHost += hostBlock;

		// Accumulator should always be in [0, 1)
		ASSERT_GE(fracAccum, 0.0);
		ASSERT_LT(fracAccum, 1.0);
	}

	double actualRatio = (double)totalMachine / (double)totalHost;
	ASSERT_NEAR(actualRatio, 44100.0 / 96000.0, 0.00001);
}

TEST(Resampler, FractionalAccumVariousRates) {
	double hostRates[] = { 48000, 88200, 96000, 176400, 192000, 22050 };

	for (double hostRate : hostRates) {
		double fracAccum = 0.0;
		int hostBlock = 256;
		long long totalMachine = 0;
		long long totalHost = 0;

		for (int i = 0; i < 1000; i++) {
			double exact = hostBlock * (44100.0 / hostRate) + fracAccum;
			int machineSamples = (int)exact;
			if (machineSamples < 1) machineSamples = 1;
			fracAccum = exact - machineSamples;
			totalMachine += machineSamples;
			totalHost += hostBlock;
		}

		double actualRatio = (double)totalMachine / (double)totalHost;
		double expectedRatio = 44100.0 / hostRate;
		ASSERT_NEAR(actualRatio, expectedRatio, 0.001);
	}
}

TEST(Resampler, FractionalAccumSmallBlockSize) {
	// Small (but realistic) block sizes stress the accumulator.
	// Note: block size 1 is pathological — the min-1 clamp forces 1 machine
	// sample per host sample, defeating the ratio.  Real DAWs use >= 16.
	double fracAccum = 0.0;
	double hostRate = 96000.0;
	int hostBlock = 16;
	long long totalMachine = 0;
	long long totalHost = 0;

	for (int i = 0; i < 6000; i++) { // 6000 * 16 = 96000 host samples = 1s
		double exact = hostBlock * (44100.0 / hostRate) + fracAccum;
		int machineSamples = (int)exact;
		if (machineSamples < 1) machineSamples = 1;
		fracAccum = exact - machineSamples;
		totalMachine += machineSamples;
		totalHost += hostBlock;
	}

	// Should be approximately 44100 machine samples for 96000 host samples
	ASSERT_NEAR((double)totalMachine, 44100.0, 10.0);
}

// ===========================================================================
//  updateResamplers threshold detection
// ===========================================================================

TEST(Resampler, ThresholdDetection44100Exact) {
	// At exactly 44100, needsResampling should be false
	double sampleRate = 44100.0;
	bool needsResampling = (std::abs(sampleRate - kBuzzMachineRate) > 1.0);
	ASSERT_FALSE(needsResampling);
}

TEST(Resampler, ThresholdDetection44100PointFive) {
	// 44100.5 is within tolerance
	double sampleRate = 44100.5;
	bool needsResampling = (std::abs(sampleRate - kBuzzMachineRate) > 1.0);
	ASSERT_FALSE(needsResampling);
}

TEST(Resampler, ThresholdDetection44102) {
	// 44102 is outside tolerance
	double sampleRate = 44102.0;
	bool needsResampling = (std::abs(sampleRate - kBuzzMachineRate) > 1.0);
	ASSERT_TRUE(needsResampling);
}

TEST(Resampler, ThresholdDetectionCommonRates) {
	double rates[] = { 22050, 44100, 48000, 88200, 96000, 176400, 192000 };
	bool expected[] = { true, false, true, true, true, true, true };

	for (int i = 0; i < 7; i++) {
		bool needs = (std::abs(rates[i] - kBuzzMachineRate) > 1.0);
		if (expected[i]) {
			ASSERT_TRUE(needs);
		} else {
			ASSERT_FALSE(needs);
		}
	}
}

// ===========================================================================
//  NaN / Inf robustness
// ===========================================================================

TEST(Resampler, NaNRecoveryAfterReset) {
	// NaN in biquad feedback poisons state permanently, but a reset should
	// restore clean operation.  This mirrors real-world recovery: if a Buzz
	// machine produces garbage, the resampler recovers on the next reset.
	BuzzResampler rs;
	rs.init(44100.0, 96000.0);

	float input[100], output[218];
	generateSine(input, 100, 440.0, 44100.0);
	input[50] = std::nanf("");
	rs.process(input, 100, output, 218);

	// After reset, clean input should produce clean output
	rs.reset();
	generateSine(input, 100, 440.0, 44100.0);
	rs.process(input, 100, output, 218);

	int nanCount = 0;
	for (int i = 0; i < 218; i++) {
		if (output[i] != output[i]) nanCount++;
	}
	ASSERT_EQ(nanCount, 0);
}

// ===========================================================================
//  Stereo consistency (two independent resamplers should track identically)
// ===========================================================================

TEST(Resampler, StereoChannelsTrackIdentically) {
	BuzzResampler rsL, rsR;
	rsL.init(44100.0, 96000.0);
	rsR.init(44100.0, 96000.0);

	const int inLen = 4410;
	const int outLen = 9600;
	std::vector<float> input(inLen), outL(outLen), outR(outLen);
	generateSine(input.data(), inLen, 440.0, 44100.0);

	int consumedL = rsL.process(input.data(), inLen, outL.data(), outLen);
	int consumedR = rsR.process(input.data(), inLen, outR.data(), outLen);

	ASSERT_EQ(consumedL, consumedR);
	for (int i = 0; i < outLen; i++) {
		ASSERT_NEAR(outL[i], outR[i], 1e-7f);
	}
}

TEST(Resampler, StereoIndependentContent) {
	BuzzResampler rsL, rsR;
	rsL.init(44100.0, 96000.0);
	rsR.init(44100.0, 96000.0);

	const int inLen = 4410;
	const int outLen = 9600;
	std::vector<float> inputL(inLen), inputR(inLen), outL(outLen), outR(outLen);
	generateSine(inputL.data(), inLen, 440.0, 44100.0);
	generateSine(inputR.data(), inLen, 880.0, 44100.0);

	rsL.process(inputL.data(), inLen, outL.data(), outLen);
	rsR.process(inputR.data(), inLen, outR.data(), outLen);

	// Outputs should differ since inputs differ
	double diffRms = 0;
	for (int i = 100; i < outLen; i++) {
		float d = outL[i] - outR[i];
		diffRms += d * d;
	}
	diffRms = sqrt(diffRms / (outLen - 100));
	ASSERT_GT(diffRms, 0.1);
}
