// Unit tests for the per-machine quirk subsystem (BuzzMachineQuirks.h).
// These tests exercise the pure-function quirk helpers against synthetic
// CMachineInfo/CMachineInterface-shaped buffers, without loading any real
// Buzz DLLs.

#include "TestFramework.h"
#include "../src/buzz/BuzzMachineQuirks.h"
#include <cstring>
#include <vector>

using namespace BuzzVst;

namespace {

// Build a stack-local CMachineInfo with just the fields the quirk helpers
// look at. Pointers in CMachineInfo are const char* (Name, ShortName), so
// passing string literals is safe.
static CMachineInfo MakeInfo(const char* name, const char* shortName = "") {
	CMachineInfo info = {};
	info.Type = MT_EFFECT;
	info.Version = MI_VERSION;
	info.Flags = 0;
	info.Name = name;
	info.ShortName = shortName;
	info.Author = "";
	return info;
}

// Allocate a zero-filled buffer that looks like a CMachineInterface
// instance for the quirk functions to operate on. Returns a pair of
// (CMachineInterface*, underlying char buffer) so the caller can both
// inspect raw offsets and pass it to ApplyPostCreateQuirks.
struct FakeMachine {
	std::vector<char> buf;
	CMachineInterface* as_iface() {
		return reinterpret_cast<CMachineInterface*>(buf.data());
	}
	char* raw() { return buf.data(); }
	// Pretend-CreateMachine: fill the buffer with a non-zero pattern, then
	// set the vtable / GlobalVals / TrackVals / AttrVals slots the way a
	// minimal ctor would (base class = offsets 0..0x17 on 32-bit).
	void fill_garbage(unsigned char byte) {
		memset(buf.data(), byte, buf.size());
	}
};

static FakeMachine MakeFake(size_t size) {
	FakeMachine fm;
	fm.buf.resize(size, 0);
	return fm;
}

} // namespace

// ============================================================================
// QuirkNameEquals / QuirkNameContains
// ============================================================================

TEST(MachineQuirks, NameEqualsMatchesCaseInsensitive) {
	ASSERT_TRUE(QuirkNameEquals("Jeskola Delay", "jeskola delay"));
	ASSERT_TRUE(QuirkNameEquals("JESKOLA DELAY", "Jeskola Delay"));
}

TEST(MachineQuirks, NameEqualsRejectsPrefix) {
	ASSERT_FALSE(QuirkNameEquals("Jeskola Cross Delay", "Jeskola Delay"));
}

TEST(MachineQuirks, NameEqualsHandlesNull) {
	ASSERT_FALSE(QuirkNameEquals(nullptr, "foo"));
	ASSERT_FALSE(QuirkNameEquals("foo", nullptr));
	ASSERT_FALSE(QuirkNameEquals(nullptr, nullptr));
}

TEST(MachineQuirks, NameContainsFindsSubstring) {
	ASSERT_TRUE(QuirkNameContains("BTDSys Frequency Bomb v2", "Frequency Bomb"));
	ASSERT_TRUE(QuirkNameContains("frequency bomb", "FREQUENCY BOMB"));
	ASSERT_TRUE(QuirkNameContains("prefix Freq Bomb suffix", "Freq Bomb"));
}

TEST(MachineQuirks, NameContainsNegative) {
	ASSERT_FALSE(QuirkNameContains("Jeskola Cross Delay", "Rosengarten"));
	ASSERT_FALSE(QuirkNameContains("foo", "foobar"));
	ASSERT_FALSE(QuirkNameContains("xyz", "abc"));
}

TEST(MachineQuirks, NameContainsEmptyNeedleMatchesAnything) {
	// Consistent with strstr semantics: _strnicmp(p, needle, 0) returns
	// 0 for any p, so the helper returns true on the first iteration.
	// The quirk tables never pass empty strings, but asserting the
	// behavior keeps a future refactor from changing it silently.
	ASSERT_TRUE(QuirkNameContains("Jeskola Cross Delay", ""));
}

TEST(MachineQuirks, NameContainsHandlesNull) {
	ASSERT_FALSE(QuirkNameContains(nullptr, "foo"));
	ASSERT_FALSE(QuirkNameContains("foo", nullptr));
}

// ============================================================================
// QuirkedInstanceSize — each known machine name returns the expected size
// ============================================================================

TEST(MachineQuirks, QuirkedSizeForUnknownMachineIsZero) {
	auto info = MakeInfo("Not A Real Machine");
	ASSERT_EQ(QuirkedInstanceSize(&info), (SIZE_T)0);
}

TEST(MachineQuirks, QuirkedSizeNullInfo) {
	ASSERT_EQ(QuirkedInstanceSize(nullptr), (SIZE_T)0);
}

TEST(MachineQuirks, QuirkedSizeJeskolaCrossDelay) {
	auto info = MakeInfo("Jeskola Cross Delay");
	ASSERT_EQ(QuirkedInstanceSize(&info), (SIZE_T)0x18c);
}

TEST(MachineQuirks, QuirkedSizeJeskolaCrossDelayByShortName) {
	auto info = MakeInfo("", "Cross Delay");
	ASSERT_EQ(QuirkedInstanceSize(&info), (SIZE_T)0x18c);
}

TEST(MachineQuirks, QuirkedSizeJeskolaDelay) {
	auto info = MakeInfo("Jeskola Delay");
	ASSERT_EQ(QuirkedInstanceSize(&info), (SIZE_T)0x11c);
}

TEST(MachineQuirks, QuirkedSizeFrequencyBomb) {
	auto info = MakeInfo("Frequency Bomb");
	ASSERT_EQ(QuirkedInstanceSize(&info), (SIZE_T)0x90);
}

TEST(MachineQuirks, QuirkedSizeFrequencyBombWithPrefix) {
	auto info = MakeInfo("BTDSys Frequency Bomb");
	ASSERT_EQ(QuirkedInstanceSize(&info), (SIZE_T)0x90);
}

TEST(MachineQuirks, QuirkedSizeRosengarten) {
	auto info = MakeInfo("Rosengarten");
	ASSERT_EQ(QuirkedInstanceSize(&info), (SIZE_T)0x66c);
}

TEST(MachineQuirks, QuirkedSizeDCEliminator) {
	auto info = MakeInfo("Automaton DC Eliminator");
	ASSERT_EQ(QuirkedInstanceSize(&info), (SIZE_T)0x2d0);
}

TEST(MachineQuirks, QuirkedSizePolyWog) {
	auto info = MakeInfo("Zephod PolyWog 22");
	ASSERT_EQ(QuirkedInstanceSize(&info), (SIZE_T)0x100);
}

TEST(MachineQuirks, QuirkedSizeDuafilt) {
	auto info = MakeInfo("Static Duafilt II");
	ASSERT_EQ(QuirkedInstanceSize(&info), (SIZE_T)0xc8);
}

TEST(MachineQuirks, QuirkedSizePearlDrum) {
	auto info = MakeInfo("7900s Pearl Drum");
	ASSERT_EQ(QuirkedInstanceSize(&info), (SIZE_T)0x29790);
}

// ============================================================================
// IsUnsupportedByLoadPath — the blacklist
// ============================================================================

TEST(MachineQuirks, BlacklistEmptyOnUnknown) {
	auto info = MakeInfo("Totally Fine Machine");
	ASSERT_FALSE(IsUnsupportedByLoadPath(&info));
}

TEST(MachineQuirks, BlacklistLostBitIPan) {
	auto info = MakeInfo("Lost_Bit iPan");
	ASSERT_TRUE(IsUnsupportedByLoadPath(&info));
}

TEST(MachineQuirks, BlacklistNullInfo) {
	ASSERT_FALSE(IsUnsupportedByLoadPath(nullptr));
}

// ============================================================================
// HasBadDestructor — destructor-skip list
// ============================================================================

TEST(MachineQuirks, BadDestructorForPolyWog) {
	auto info = MakeInfo("Zephod PolyWog 22");
	ASSERT_TRUE(HasBadDestructor(&info));
}

TEST(MachineQuirks, BadDestructorForPearlDrum) {
	auto info = MakeInfo("7900s Pearl Drum");
	ASSERT_TRUE(HasBadDestructor(&info));
}

TEST(MachineQuirks, BadDestructorForJoyControl) {
	auto info = MakeInfo("kazuya JoyControl-4");
	ASSERT_TRUE(HasBadDestructor(&info));
}

TEST(MachineQuirks, BadDestructorForReSaw) {
	auto info = MakeInfo("Zephod ReSaw");
	ASSERT_TRUE(HasBadDestructor(&info));
}

TEST(MachineQuirks, BadDestructorForHarcsVision) {
	auto info = MakeInfo("Harcs Vision");
	ASSERT_TRUE(HasBadDestructor(&info));
}

TEST(MachineQuirks, BadDestructorForDuafilt) {
	auto info = MakeInfo("Static Duafilt II");
	ASSERT_TRUE(HasBadDestructor(&info));
}

TEST(MachineQuirks, BadDestructorFalseForWellBehavedMachine) {
	// Machines in the quirk table but not in the bad-destructor list.
	auto a = MakeInfo("Jeskola Delay");
	auto b = MakeInfo("Automaton DC Eliminator");
	auto c = MakeInfo("Some Other Machine");
	ASSERT_FALSE(HasBadDestructor(&a));
	ASSERT_FALSE(HasBadDestructor(&b));
	ASSERT_FALSE(HasBadDestructor(&c));
}

// ============================================================================
// ZeroRangePreserving — core memset-with-preserves primitive
// ============================================================================

TEST(MachineQuirks, ZeroRangePreservingZerosWholeRange) {
	char buf[64];
	memset(buf, 0xAB, sizeof(buf));
	ZeroRangePreserving(buf, 0, sizeof(buf), nullptr, 0);
	for (size_t i = 0; i < sizeof(buf); i++) {
		ASSERT_EQ((unsigned char)buf[i], 0u);
	}
}

TEST(MachineQuirks, ZeroRangePreservingLeavesBytesOutsideRange) {
	char buf[64];
	memset(buf, 0xAB, sizeof(buf));
	ZeroRangePreserving(buf, 16, 48, nullptr, 0);
	for (size_t i = 0; i < 16; i++)
		ASSERT_EQ((unsigned char)buf[i], 0xABu);
	for (size_t i = 16; i < 48; i++)
		ASSERT_EQ((unsigned char)buf[i], 0u);
	for (size_t i = 48; i < sizeof(buf); i++)
		ASSERT_EQ((unsigned char)buf[i], 0xABu);
}

TEST(MachineQuirks, ZeroRangePreservingKeepsPreservedOffsets) {
	char buf[64] = {};
	// Write a sentinel pointer-sized value at offsets 0x10 and 0x20.
	*reinterpret_cast<uint32_t*>(buf + 0x10) = 0xDEADBEEF;
	*reinterpret_cast<uint32_t*>(buf + 0x20) = 0xCAFEBABE;
	SIZE_T preserves[2] = { 0x10, 0x20 };
	ZeroRangePreserving(buf, 0, sizeof(buf), preserves, 2);
	ASSERT_EQ(*reinterpret_cast<uint32_t*>(buf + 0x10), 0xDEADBEEFu);
	ASSERT_EQ(*reinterpret_cast<uint32_t*>(buf + 0x20), 0xCAFEBABEu);
	// Everything else should be zero.
	for (size_t i = 0; i < sizeof(buf); i++) {
		if (i >= 0x10 && i < 0x14) continue;
		if (i >= 0x20 && i < 0x24) continue;
		ASSERT_EQ((unsigned char)buf[i], 0u);
	}
}

TEST(MachineQuirks, ZeroRangePreservingIgnoresEmptyRange) {
	char buf[32];
	memset(buf, 0x5A, sizeof(buf));
	ZeroRangePreserving(buf, 8, 8, nullptr, 0);  // start == end
	for (size_t i = 0; i < sizeof(buf); i++) {
		ASSERT_EQ((unsigned char)buf[i], 0x5Au);
	}
}

// ============================================================================
// ApplyPostCreateQuirks — end-to-end on synthetic machine buffers
// ============================================================================

TEST(MachineQuirks, ApplyPostCreateNullSafe) {
	// Should not crash with null pointers.
	ApplyPostCreateQuirks(nullptr, nullptr);
	auto info = MakeInfo("Jeskola Delay");
	ApplyPostCreateQuirks(nullptr, &info);
	auto fm = MakeFake(0x11c);
	ApplyPostCreateQuirks(fm.as_iface(), nullptr);
}

TEST(MachineQuirks, ApplyPostCreateNoopForUnknownMachine) {
	auto fm = MakeFake(0x100);
	fm.fill_garbage(0xCC);
	auto info = MakeInfo("Not A Real Machine");
	ApplyPostCreateQuirks(fm.as_iface(), &info);
	// Buffer should be unchanged.
	for (size_t i = 0; i < fm.buf.size(); i++)
		ASSERT_EQ((unsigned char)fm.buf[i], 0xCCu);
}

TEST(MachineQuirks, ApplyPostCreateJeskolaDelayZeroesFromBaseClassEnd) {
	auto fm = MakeFake(0x11c);
	fm.fill_garbage(0xCC);
	auto info = MakeInfo("Jeskola Delay");
	ApplyPostCreateQuirks(fm.as_iface(), &info);
	// Base class [0..0x18) preserved as 0xCC (we only touch derived fields).
	for (size_t i = 0; i < 0x18; i++)
		ASSERT_EQ((unsigned char)fm.buf[i], 0xCCu);
	// Derived [0x18..0x11c) must be zero.
	for (size_t i = 0x18; i < 0x11c; i++)
		ASSERT_EQ((unsigned char)fm.buf[i], 0u);
}

TEST(MachineQuirks, ApplyPostCreateDuafiltPreservesSubObjVtable) {
	auto fm = MakeFake(0xc8);
	fm.fill_garbage(0xCC);
	// Static Duafilt II's ctor writes a sub-object vtable at +0x1c.
	// ApplyPostCreateQuirks must NOT wipe it.
	*reinterpret_cast<uint32_t*>(fm.raw() + 0x1c) = 0x100031A0;
	auto info = MakeInfo("Static Duafilt II");
	ApplyPostCreateQuirks(fm.as_iface(), &info);
	ASSERT_EQ(*reinterpret_cast<uint32_t*>(fm.raw() + 0x1c), 0x100031A0u);
	// Bytes on either side of the preserved word should now be zero.
	ASSERT_EQ((unsigned char)fm.buf[0x18], 0u);
	ASSERT_EQ((unsigned char)fm.buf[0x19], 0u);
	ASSERT_EQ((unsigned char)fm.buf[0x1a], 0u);
	ASSERT_EQ((unsigned char)fm.buf[0x1b], 0u);
	ASSERT_EQ((unsigned char)fm.buf[0x20], 0u);
	ASSERT_EQ((unsigned char)fm.buf[0x21], 0u);
}

TEST(MachineQuirks, ApplyPostCreatePearlDrumPreservesOffset20) {
	// Pearl Drum's quirk has size 0x29790; make a smaller buffer just
	// large enough to contain the preserved offset plus a bit of slop,
	// and give the rest zero so we can assert the preserved word survives.
	// NOTE: we allocate the full 0x29790 because the quirk memsets up to
	// the end — anything short would be a buffer overflow.
	auto fm = MakeFake(0x29790);
	fm.fill_garbage(0xCC);
	*reinterpret_cast<uint32_t*>(fm.raw() + 0x20) = 0x10008cb8;
	auto info = MakeInfo("7900s Pearl Drum");
	ApplyPostCreateQuirks(fm.as_iface(), &info);
	ASSERT_EQ(*reinterpret_cast<uint32_t*>(fm.raw() + 0x20), 0x10008cb8u);
	// Base class preserved.
	for (size_t i = 0; i < 0x18; i++)
		ASSERT_EQ((unsigned char)fm.buf[i], 0xCCu);
	// Bytes immediately before and after the preserved word are zero.
	for (size_t i = 0x18; i < 0x20; i++)
		ASSERT_EQ((unsigned char)fm.buf[i], 0u);
	for (size_t i = 0x24; i < 0x30; i++)
		ASSERT_EQ((unsigned char)fm.buf[i], 0u);
}

TEST(MachineQuirks, ApplyPostCreateDCEliminatorNullsOffset18) {
	// DC Eliminator isn't a "blanket zero" machine — it gets a targeted
	// null of this+0x18 and relies on its own ctor to have initialized
	// everything else.
	auto fm = MakeFake(0x2d0);
	fm.fill_garbage(0xCC);
	auto info = MakeInfo("Automaton DC Eliminator");
	ApplyPostCreateQuirks(fm.as_iface(), &info);
	// Offset 0x18 must be null.
	ASSERT_EQ(*reinterpret_cast<uint32_t*>(fm.raw() + 0x18), 0u);
	// Everything else should be unchanged — verify a few sentinel offsets.
	ASSERT_EQ((unsigned char)fm.buf[0x00], 0xCCu);
	ASSERT_EQ((unsigned char)fm.buf[0x17], 0xCCu);
	ASSERT_EQ((unsigned char)fm.buf[0x1c], 0xCCu);
	ASSERT_EQ((unsigned char)fm.buf[0x24], 0xCCu);
	ASSERT_EQ((unsigned char)fm.buf[0x2cf], 0xCCu);
}

TEST(MachineQuirks, ApplyPostCreateRosengartenNullsPerTrackPointers) {
	auto fm = MakeFake(0x66c);
	fm.fill_garbage(0xCC);
	auto info = MakeInfo("Rosengarten");
	ApplyPostCreateQuirks(fm.as_iface(), &info);
	// For each of 8 sub-objects at this+0xcc + t*0xb4, sub+4 and sub+8
	// must now be null. Sub+0 (vtable), sub+12 onward should be unchanged.
	for (int t = 0; t < 8; t++) {
		size_t sub = 0xcc + (size_t)t * 0xb4;
		ASSERT_EQ((unsigned char)fm.buf[sub + 0], 0xCCu);      // vtable slot untouched
		ASSERT_EQ(*reinterpret_cast<uint32_t*>(fm.raw() + sub + 4), 0u);
		ASSERT_EQ(*reinterpret_cast<uint32_t*>(fm.raw() + sub + 8), 0u);
		ASSERT_EQ((unsigned char)fm.buf[sub + 12], 0xCCu);     // next field untouched
	}
	// Bytes outside the sub-object region untouched too (spot checks).
	ASSERT_EQ((unsigned char)fm.buf[0x00], 0xCCu);
	ASSERT_EQ((unsigned char)fm.buf[0xcb], 0xCCu);
}

// ============================================================================
// Quirk-list consistency
// ============================================================================

TEST(MachineQuirks, EveryBadDestructorMachineIsEitherQuirkedOrKnownWellDefined) {
	// HasBadDestructor should be consistent with what we've documented.
	// Machines in the bad-dtor list may or may not also be in the quirked
	// list, but for the ones that are, both tables should agree on the
	// instance size being >0 (meaning we know their layout).
	struct Entry { const char* name; bool expectedQuirked; };
	Entry entries[] = {
		{ "Zephod PolyWog 22",   true },
		{ "Static Duafilt II",   true },
		{ "7900s Pearl Drum",    true },
		{ "Zephod ReSaw",        false }, // not in QuirkedInstanceSize
		{ "Harcs Vision",        false },
		{ "kazuya JoyControl-4", false },
	};
	for (auto& e : entries) {
		auto info = MakeInfo(e.name);
		CHECK_TRUE(HasBadDestructor(&info));
		if (e.expectedQuirked) {
			CHECK_NE(QuirkedInstanceSize(&info), (SIZE_T)0);
		}
	}
}

TEST(MachineQuirks, QuirkTableAndBlacklistAreDisjoint) {
	// A machine should be in at most one of the two lists.
	const char* quirked[] = {
		"Jeskola Cross Delay",
		"Jeskola Delay",
		"Frequency Bomb",
		"Rosengarten",
		"Automaton DC Eliminator",
		"Zephod PolyWog 22",
		"Static Duafilt II",
		"7900s Pearl Drum",
	};
	for (auto* name : quirked) {
		auto info = MakeInfo(name);
		CHECK_NE(QuirkedInstanceSize(&info), (SIZE_T)0);
		CHECK_TRUE(!IsUnsupportedByLoadPath(&info));
	}
}
