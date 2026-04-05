#pragma once

// Streaming sample-rate converter for bridging Buzz machines (44100 Hz) to
// arbitrary VST host sample rates.  Uses cubic Hermite (Catmull-Rom)
// interpolation with a cascaded biquad anti-alias filter for downsampling.
//
// The resampler maintains phase and filter state between process() calls,
// producing seamless audio across block boundaries.

#include <cmath>
#include <cstring>

namespace BuzzVst {

static const double kBuzzMachineRate = 44100.0;

// 2nd-order biquad section (transposed direct-form II)
struct Biquad {
	double b0, b1, b2, a1, a2;
	double z1 = 0.0, z2 = 0.0;

	void reset() { z1 = z2 = 0.0; }

	float process(float in) {
		double out = b0 * in + z1;
		z1 = b1 * in - a1 * out + z2;
		z2 = b2 * in - a2 * out;
		return (float)out;
	}

	// Butterworth lowpass (Q = 1/sqrt(2))
	static Biquad lowpass(double sampleRate, double cutoff) {
		const double pi = 3.14159265358979323846;
		double w0 = 2.0 * pi * cutoff / sampleRate;
		double cosw0 = cos(w0);
		double sinw0 = sin(w0);
		double alpha = sinw0 / (2.0 * 0.7071067811865476);

		double a0 = 1.0 + alpha;
		Biquad bq;
		bq.b0 = ((1.0 - cosw0) / 2.0) / a0;
		bq.b1 = (1.0 - cosw0) / a0;
		bq.b2 = bq.b0;
		bq.a1 = (-2.0 * cosw0) / a0;
		bq.a2 = (1.0 - alpha) / a0;
		return bq;
	}
};

// Streaming resampler: converts srcRate audio to dstRate audio using cubic
// Hermite interpolation.  When downsampling (srcRate > dstRate), a 4th-order
// Butterworth anti-alias filter is applied to the source before interpolation.
class BuzzResampler {
public:
	BuzzResampler() = default;

	void init(double srcRate, double dstRate) {
		ratio_ = srcRate / dstRate;
		phase_ = 0.0;
		memset(hist_, 0, sizeof(hist_));
		active_ = true;

		// Anti-alias filter for downsampling (cascade of two 2nd-order sections
		// gives a 4th-order Butterworth at 90% of destination Nyquist)
		antiAlias_ = (srcRate > dstRate);
		if (antiAlias_) {
			double cutoff = (dstRate * 0.5) * 0.9;
			aa1_ = Biquad::lowpass(srcRate, cutoff);
			aa2_ = Biquad::lowpass(srcRate, cutoff);
		}
	}

	void reset() {
		phase_ = 0.0;
		memset(hist_, 0, sizeof(hist_));
		if (antiAlias_) { aa1_.reset(); aa2_.reset(); }
	}

	bool isActive() const { return active_; }

	// Produce exactly outputLen output samples from up to inputLen input samples.
	// Returns the number of input samples consumed.  The internal phase state
	// carries over between calls so that consecutive blocks are seamless.
	int process(const float* input, int inputLen, float* output, int outputLen) {
		int inPos = 0;

		for (int o = 0; o < outputLen; o++) {
			// Consume input samples as the phase crosses integer boundaries
			while (phase_ >= 1.0) {
				float s = (inPos < inputLen) ? input[inPos++] : 0.0f;
				if (antiAlias_) { s = aa1_.process(s); s = aa2_.process(s); }
				hist_[0] = hist_[1];
				hist_[1] = hist_[2];
				hist_[2] = hist_[3];
				hist_[3] = s;
				phase_ -= 1.0;
			}

			// Cubic Hermite (Catmull-Rom) interpolation at fractional position
			float t  = (float)phase_;
			float c0 = hist_[1];
			float c1 = 0.5f * (hist_[2] - hist_[0]);
			float c2 = hist_[0] - 2.5f * hist_[1] + 2.0f * hist_[2] - 0.5f * hist_[3];
			float c3 = 0.5f * (hist_[3] - hist_[0]) + 1.5f * (hist_[1] - hist_[2]);
			output[o] = ((c3 * t + c2) * t + c1) * t + c0;

			phase_ += ratio_;
		}

		return inPos;
	}

private:
	double ratio_  = 1.0;
	double phase_  = 0.0;
	float  hist_[4] = {};
	bool   active_    = false;
	bool   antiAlias_ = false;
	Biquad aa1_, aa2_;
};

} // namespace BuzzVst
