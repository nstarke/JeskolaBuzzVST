#include "TestFramework.h"
#include "TestHelpers.h"
#include "../src/vst3/ParameterMapping.h"

using namespace BuzzVst;

// ===== NormalizedToBuzz =====

TEST(ParamMapping, NormalizedToBuzzMinimum) {
	CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);
	ASSERT_EQ(ParameterMapping::NormalizedToBuzz(0.0, &p), 0);
}

TEST(ParamMapping, NormalizedToBuzzMaximum) {
	CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);
	ASSERT_EQ(ParameterMapping::NormalizedToBuzz(1.0, &p), 127);
}

TEST(ParamMapping, NormalizedToBuzzMidpoint) {
	CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);
	int val = ParameterMapping::NormalizedToBuzz(0.5, &p);
	// 0 + 0.5 * 127 + 0.5 = 64.0 -> 64
	ASSERT_GE(val, 63);
	ASSERT_LE(val, 64);
}

TEST(ParamMapping, NormalizedToBuzzWithOffset) {
	// Range 10..110
	CMachineParameter p = MakeParam(pt_byte, 10, 110, 255, 60);
	ASSERT_EQ(ParameterMapping::NormalizedToBuzz(0.0, &p), 10);
	ASSERT_EQ(ParameterMapping::NormalizedToBuzz(1.0, &p), 110);
	int mid = ParameterMapping::NormalizedToBuzz(0.5, &p);
	ASSERT_GE(mid, 59);
	ASSERT_LE(mid, 61);
}

TEST(ParamMapping, NormalizedToBuzzWordRange) {
	CMachineParameter p = MakeParam(pt_word, 0, 65535, 65535, 32768);
	ASSERT_EQ(ParameterMapping::NormalizedToBuzz(0.0, &p), 0);
	ASSERT_EQ(ParameterMapping::NormalizedToBuzz(1.0, &p), 65535);
}

TEST(ParamMapping, NormalizedToBuzzClampsBelow) {
	CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);
	ASSERT_EQ(ParameterMapping::NormalizedToBuzz(-0.5, &p), 0);
}

TEST(ParamMapping, NormalizedToBuzzClampsAbove) {
	CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);
	ASSERT_EQ(ParameterMapping::NormalizedToBuzz(1.5, &p), 127);
}

TEST(ParamMapping, NormalizedToBuzzNullParam) {
	ASSERT_EQ(ParameterMapping::NormalizedToBuzz(0.5, nullptr), 0);
}

// ===== BuzzToNormalized =====

TEST(ParamMapping, BuzzToNormalizedMin) {
	CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);
	ASSERT_NEAR(ParameterMapping::BuzzToNormalized(0, &p), 0.0, 0.001);
}

TEST(ParamMapping, BuzzToNormalizedMax) {
	CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);
	ASSERT_NEAR(ParameterMapping::BuzzToNormalized(127, &p), 1.0, 0.001);
}

TEST(ParamMapping, BuzzToNormalizedMid) {
	CMachineParameter p = MakeParam(pt_byte, 0, 100, 255, 50);
	double norm = ParameterMapping::BuzzToNormalized(50, &p);
	ASSERT_NEAR(norm, 0.5, 0.001);
}

TEST(ParamMapping, BuzzToNormalizedWithOffset) {
	CMachineParameter p = MakeParam(pt_byte, 10, 110, 255, 60);
	ASSERT_NEAR(ParameterMapping::BuzzToNormalized(10, &p), 0.0, 0.001);
	ASSERT_NEAR(ParameterMapping::BuzzToNormalized(110, &p), 1.0, 0.001);
	ASSERT_NEAR(ParameterMapping::BuzzToNormalized(60, &p), 0.5, 0.001);
}

TEST(ParamMapping, BuzzToNormalizedClamped) {
	CMachineParameter p = MakeParam(pt_byte, 0, 100, 255, 50);
	double norm = ParameterMapping::BuzzToNormalized(200, &p);
	ASSERT_LE(norm, 1.0);
}

TEST(ParamMapping, BuzzToNormalizedNullParam) {
	ASSERT_NEAR(ParameterMapping::BuzzToNormalized(50, nullptr), 0.0, 0.001);
}

// ===== Roundtrip =====

TEST(ParamMapping, RoundtripByte) {
	CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);

	for (int i = 0; i <= 127; i++) {
		double norm = ParameterMapping::BuzzToNormalized(i, &p);
		int back = ParameterMapping::NormalizedToBuzz(norm, &p);
		CHECK_EQ(back, i);
	}
}

TEST(ParamMapping, RoundtripWord) {
	CMachineParameter p = MakeParam(pt_word, 0, 1000, 65535, 500);

	// Test selected values across the range
	for (int i = 0; i <= 1000; i += 10) {
		double norm = ParameterMapping::BuzzToNormalized(i, &p);
		int back = ParameterMapping::NormalizedToBuzz(norm, &p);
		CHECK_EQ(back, i);
	}
}

TEST(ParamMapping, RoundtripWithOffset) {
	CMachineParameter p = MakeParam(pt_byte, 20, 100, 255, 60);

	for (int i = 20; i <= 100; i++) {
		double norm = ParameterMapping::BuzzToNormalized(i, &p);
		int back = ParameterMapping::NormalizedToBuzz(norm, &p);
		CHECK_EQ(back, i);
	}
}

// ===== GetDefaultNormalized =====

TEST(ParamMapping, DefaultNormalizedStateParam) {
	CMachineParameter p = MakeParam(pt_byte, 0, 100, 255, 50, MPF_STATE);
	double def = ParameterMapping::GetDefaultNormalized(&p);
	ASSERT_NEAR(def, 0.5, 0.001);
}

TEST(ParamMapping, DefaultNormalizedNonStateParam) {
	CMachineParameter p = MakeParam(pt_byte, 0, 100, 255, 50, 0);
	double def = ParameterMapping::GetDefaultNormalized(&p);
	ASSERT_NEAR(def, 0.0, 0.001);
}

// ===== GetStepCount =====

TEST(ParamMapping, StepCountByte) {
	CMachineParameter p = MakeParam(pt_byte, 0, 127, 255, 64);
	ASSERT_EQ(ParameterMapping::GetStepCount(&p), 127);
}

TEST(ParamMapping, StepCountWord) {
	CMachineParameter p = MakeParam(pt_word, 0, 65535, 65535, 0);
	ASSERT_EQ(ParameterMapping::GetStepCount(&p), 65535);
}

TEST(ParamMapping, StepCountSwitch) {
	CMachineParameter p = MakeParam(pt_switch, 0, 1, 255, 0);
	ASSERT_EQ(ParameterMapping::GetStepCount(&p), 1);
}

TEST(ParamMapping, StepCountNull) {
	ASSERT_EQ(ParameterMapping::GetStepCount(nullptr), 0);
}

// ===== Zero-range edge case =====

TEST(ParamMapping, ZeroRange) {
	CMachineParameter p = MakeParam(pt_byte, 50, 50, 255, 50);
	// Min == Max -> range is 0
	ASSERT_NEAR(ParameterMapping::BuzzToNormalized(50, &p), 0.0, 0.001);
	ASSERT_EQ(ParameterMapping::NormalizedToBuzz(0.5, &p), 50);
}
