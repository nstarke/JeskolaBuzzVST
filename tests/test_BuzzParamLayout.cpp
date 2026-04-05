#include "TestFramework.h"
#include "TestHelpers.h"
#include "../src/buzz/BuzzParamLayout.h"

using namespace BuzzVst;

// ===== GetParamByteSize =====

TEST(ParamLayout, ByteSizeByte) {
	ASSERT_EQ(BuzzParamLayout::GetParamByteSize(pt_byte), 1);
}

TEST(ParamLayout, ByteSizeSwitch) {
	ASSERT_EQ(BuzzParamLayout::GetParamByteSize(pt_switch), 1);
}

TEST(ParamLayout, ByteSizeWord) {
	ASSERT_EQ(BuzzParamLayout::GetParamByteSize(pt_word), 2);
}

TEST(ParamLayout, ByteSizeNote) {
	ASSERT_EQ(BuzzParamLayout::GetParamByteSize(pt_note), 1);
}

TEST(ParamLayout, ByteSizeInternal) {
	ASSERT_EQ(BuzzParamLayout::GetParamByteSize(pt_internal), 0);
}

// ===== Build with empty info =====

TEST(ParamLayout, BuildNull) {
	BuzzParamLayout layout;
	layout.Build(nullptr);
	ASSERT_EQ(layout.GetGlobalStructSize(), 0);
	ASSERT_EQ(layout.GetTrackStructSize(), 0);
	ASSERT_EQ(layout.GetTotalParamCount(), 0);
}

// ===== Build with global params only =====

TEST(ParamLayout, BuildGlobalOnly) {
	CMachineParameter p1 = MakeParam(pt_byte, 0, 127, 255, 64);
	CMachineParameter p2 = MakeParam(pt_word, 0, 65535, 65535, 1000);
	CMachineParameter p3 = MakeParam(pt_switch, 0, 1, 255, 0);

	const CMachineParameter* params[] = { &p1, &p2, &p3 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 3;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	// byte(1) + word(2) + switch(1) = 4 bytes
	ASSERT_EQ(layout.GetGlobalStructSize(), 4);
	ASSERT_EQ(layout.GetTrackStructSize(), 0);
	ASSERT_EQ(layout.GetTotalParamCount(), 3);

	auto& slots = layout.GetGlobalSlots();
	ASSERT_EQ((int)slots.size(), 3);

	// Check byte offsets
	ASSERT_EQ(slots[0].byteOffset, 0);
	ASSERT_EQ(slots[0].byteSize, 1);
	ASSERT_EQ(slots[1].byteOffset, 1);
	ASSERT_EQ(slots[1].byteSize, 2);
	ASSERT_EQ(slots[2].byteOffset, 3);
	ASSERT_EQ(slots[2].byteSize, 1);
}

// ===== Build with global + track params =====

TEST(ParamLayout, BuildGlobalAndTrack) {
	CMachineParameter gp1 = MakeParam(pt_byte, 0, 127, 255, 64);
	CMachineParameter tp1 = MakeParam(pt_note, 1, 156, 0, 0, 0);
	CMachineParameter tp2 = MakeParam(pt_byte, 0, 127, 255, 80);

	const CMachineParameter* params[] = { &gp1, &tp1, &tp2 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 2;
	info.Parameters = params;
	info.minTracks = 1;
	info.maxTracks = 8;

	BuzzParamLayout layout;
	layout.Build(&info);

	ASSERT_EQ(layout.GetGlobalStructSize(), 1);  // 1 byte param
	ASSERT_EQ(layout.GetTrackStructSize(), 2);    // note(1) + byte(1)
	ASSERT_EQ(layout.GetTotalParamCount(), 3);

	auto& tSlots = layout.GetTrackSlots();
	ASSERT_EQ((int)tSlots.size(), 2);
	ASSERT_EQ(tSlots[0].byteOffset, 0);
	ASSERT_EQ(tSlots[0].byteSize, 1);  // note = byte
	ASSERT_EQ(tSlots[1].byteOffset, 1);
	ASSERT_EQ(tSlots[1].byteSize, 1);  // byte
}

// ===== Read/Write global params =====

TEST(ParamLayout, WriteAndReadGlobalByte) {
	CMachineParameter p1 = MakeParam(pt_byte, 0, 127, 255, 64);
	const CMachineParameter* params[] = { &p1 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char buf[16] = {};
	layout.WriteGlobalParam(buf, 0, 42);
	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), 42);
}

TEST(ParamLayout, WriteAndReadGlobalWord) {
	CMachineParameter p1 = MakeParam(pt_word, 0, 65535, 65535, 1000);
	const CMachineParameter* params[] = { &p1 };

	CMachineInfo info = {};
	info.Type = MT_EFFECT;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char buf[16] = {};
	layout.WriteGlobalParam(buf, 0, 12345);
	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), 12345);
}

TEST(ParamLayout, WriteAndReadMultipleGlobals) {
	CMachineParameter p1 = MakeParam(pt_byte, 0, 255, 255, 128);
	CMachineParameter p2 = MakeParam(pt_word, 0, 10000, 65535, 5000);
	CMachineParameter p3 = MakeParam(pt_byte, 0, 1, 255, 0);

	const CMachineParameter* params[] = { &p1, &p2, &p3 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 3;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char buf[16] = {};

	layout.WriteGlobalParam(buf, 0, 200);
	layout.WriteGlobalParam(buf, 1, 9999);
	layout.WriteGlobalParam(buf, 2, 1);

	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), 200);
	ASSERT_EQ(layout.ReadGlobalParam(buf, 1), 9999);
	ASSERT_EQ(layout.ReadGlobalParam(buf, 2), 1);
}

// ===== NoValue protocol =====

TEST(ParamLayout, WriteAllNoValues) {
	CMachineParameter p1 = MakeParam(pt_byte, 0, 127, 255, 64);
	CMachineParameter p2 = MakeParam(pt_word, 0, 65535, 65535, 1000);

	const CMachineParameter* params[] = { &p1, &p2 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 2;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char buf[16] = {};

	// Write actual values first
	layout.WriteGlobalParam(buf, 0, 42);
	layout.WriteGlobalParam(buf, 1, 1234);

	// Now reset to NoValue
	layout.WriteAllNoValues(buf);

	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), 255);    // p1.NoValue
	ASSERT_EQ(layout.ReadGlobalParam(buf, 1), 65535);   // p2.NoValue
}

// ===== Write defaults =====

TEST(ParamLayout, WriteAllDefaults) {
	CMachineParameter p1 = MakeParam(pt_byte, 0, 127, 255, 64, MPF_STATE);
	CMachineParameter p2 = MakeParam(pt_word, 0, 65535, 65535, 1000, 0); // no MPF_STATE

	const CMachineParameter* params[] = { &p1, &p2 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 2;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char buf[16] = {};
	layout.WriteAllDefaults(buf);

	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), 64);      // DefValue for state param
	ASSERT_EQ(layout.ReadGlobalParam(buf, 1), 65535);    // NoValue for non-state param
}

// ===== Track parameters =====

TEST(ParamLayout, WriteAndReadTrackParams) {
	CMachineParameter gp = MakeParam(pt_byte, 0, 127, 255, 0);
	CMachineParameter tp1 = MakeParam(pt_note, 1, 156, 0, 0, 0);
	CMachineParameter tp2 = MakeParam(pt_byte, 0, 127, 255, 80);

	const CMachineParameter* params[] = { &gp, &tp1, &tp2 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 2;
	info.Parameters = params;
	info.minTracks = 1;
	info.maxTracks = 4;

	BuzzParamLayout layout;
	layout.Build(&info);

	// Allocate buffer for 2 tracks (each 3 bytes: note(2) + byte(1))
	unsigned char trackBuf[16] = {};

	// Write to track 0
	layout.WriteTrackParam(trackBuf, 0, 0, 0x41);  // note C-4
	layout.WriteTrackParam(trackBuf, 0, 1, 100);

	// Write to track 1
	layout.WriteTrackParam(trackBuf, 1, 0, 0x52);  // note D-5
	layout.WriteTrackParam(trackBuf, 1, 1, 50);

	// Read back
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 0), 0x41);
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 1), 100);
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 1, 0), 0x52);
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 1, 1), 50);
}

TEST(ParamLayout, WriteTrackAllNoValues) {
	CMachineParameter gp = MakeParam(pt_byte, 0, 127, 255, 0);
	CMachineParameter tp1 = MakeParam(pt_note, 1, 156, 0, 0, 0);
	CMachineParameter tp2 = MakeParam(pt_byte, 0, 127, 255, 80);

	const CMachineParameter* params[] = { &gp, &tp1, &tp2 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 2;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char trackBuf[16] = {};

	// Write values
	layout.WriteTrackParam(trackBuf, 0, 0, 0x41);
	layout.WriteTrackParam(trackBuf, 0, 1, 100);

	// Reset
	layout.WriteTrackAllNoValues(trackBuf, 1);

	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 0), 0);    // note NoValue = 0
	ASSERT_EQ(layout.ReadTrackParam(trackBuf, 0, 1), 255);  // byte NoValue = 255
}

// ===== Edge cases =====

TEST(ParamLayout, WriteOutOfBoundsSlotIgnored) {
	CMachineParameter p1 = MakeParam(pt_byte, 0, 127, 255, 64);
	const CMachineParameter* params[] = { &p1 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char buf[16] = {};
	// Should not crash - out of bounds slot
	layout.WriteGlobalParam(buf, 5, 42);
	layout.WriteGlobalParam(buf, -1, 42);
	layout.WriteGlobalParam(nullptr, 0, 42);
	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), 0); // unchanged
}

TEST(ParamLayout, ReadNullBuffer) {
	CMachineParameter p1 = MakeParam(pt_byte, 0, 127, 255, 64);
	const CMachineParameter* params[] = { &p1 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	ASSERT_EQ(layout.ReadGlobalParam(nullptr, 0), 0);
}

// ===== Byte truncation =====

TEST(ParamLayout, ByteParamTruncatesTo8Bits) {
	CMachineParameter p1 = MakeParam(pt_byte, 0, 255, 255, 0);
	const CMachineParameter* params[] = { &p1 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char buf[16] = {};
	layout.WriteGlobalParam(buf, 0, 0x1FF); // 511 - should truncate to 0xFF
	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), 0xFF);
}

TEST(ParamLayout, WordParamTruncatesTo16Bits) {
	CMachineParameter p1 = MakeParam(pt_word, 0, 65535, 65535, 0);
	const CMachineParameter* params[] = { &p1 };

	CMachineInfo info = {};
	info.Type = MT_GENERATOR;
	info.numGlobalParameters = 1;
	info.numTrackParameters = 0;
	info.Parameters = params;

	BuzzParamLayout layout;
	layout.Build(&info);

	unsigned char buf[16] = {};
	layout.WriteGlobalParam(buf, 0, 0x1FFFF); // should truncate to 0xFFFF
	ASSERT_EQ(layout.ReadGlobalParam(buf, 0), 0xFFFF);
}
