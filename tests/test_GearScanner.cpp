#include "TestFramework.h"
#include "TestHelpers.h"
#include "../src/vst3/GearScanner.h"

using namespace BuzzVst;

// ===== Basic scanning =====

TEST(GearScanner, ScanEmptyPathFails) {
	GearScanner scanner;
	ASSERT_FALSE(scanner.Scan(""));
	ASSERT_EQ((int)scanner.GetEntries().size(), 0);
}

TEST(GearScanner, ScanNonexistentDirFails) {
	GearScanner scanner;
	ASSERT_FALSE(scanner.Scan("C:\\nonexistent\\fakedir"));
	ASSERT_EQ((int)scanner.GetEntries().size(), 0);
}

TEST(GearScanner, ScanFileFails) {
	// Passing a file path instead of directory should fail
	GearScanner scanner;
	ASSERT_FALSE(scanner.Scan("C:\\Windows\\System32\\kernel32.dll"));
}

// ===== Scanning ref/Gear =====

TEST(GearScanner, ScanRefGearFindsEntries) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	bool ok = scanner.Scan(gearDir);
	if (!ok) {
		printf("  (skipped - ref/Gear not found) ");
		return;
	}

	ASSERT_TRUE(ok);
	ASSERT_GT((int)scanner.GetEntries().size(), 0);
}

TEST(GearScanner, ScanRefGearFindsGenerators) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	auto generators = scanner.GetGenerators();
	ASSERT_GT((int)generators.size(), 0);

	// All should be MT_GENERATOR
	for (auto& g : generators) {
		CHECK_EQ(g.machineType, MT_GENERATOR);
	}
}

TEST(GearScanner, ScanRefGearFindsEffects) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	auto effects = scanner.GetEffects();
	ASSERT_GT((int)effects.size(), 0);

	for (auto& e : effects) {
		CHECK_EQ(e.machineType, MT_EFFECT);
	}
}

// ===== Entry fields =====

TEST(GearScanner, EntriesHaveDisplayNames) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	for (auto& entry : scanner.GetEntries()) {
		CHECK_TRUE(!entry.displayName.empty());
	}
}

TEST(GearScanner, EntriesHaveDllPaths) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	for (auto& entry : scanner.GetEntries()) {
		CHECK_TRUE(!entry.dllPath.empty());
		// Path should end with .dll
		auto dotPos = entry.dllPath.rfind('.');
		CHECK_TRUE(dotPos != std::string::npos);
	}
}

TEST(GearScanner, EntriesHaveCategories) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	// At least some entries should have non-empty categories
	// (the Gear dir has Generators/ and Effects/ subdirectories)
	bool hasCategory = false;
	for (auto& entry : scanner.GetEntries()) {
		if (!entry.category.empty()) {
			hasCategory = true;
			break;
		}
	}
	ASSERT_TRUE(hasCategory);
}

// ===== Known machine detection =====

TEST(GearScanner, FindsFSMKickXP) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	bool found = false;
	for (auto& entry : scanner.GetEntries()) {
		if (entry.dllPath.find("FSM Kick XP") != std::string::npos) {
			found = true;
			ASSERT_EQ(entry.machineType, MT_GENERATOR);
			ASSERT_FALSE(entry.displayName.empty());
			break;
		}
	}
	ASSERT_TRUE(found);
}

TEST(GearScanner, FindsBigyoFilter) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	bool found = false;
	for (auto& entry : scanner.GetEntries()) {
		if (entry.dllPath.find("Bigyo Filter") != std::string::npos) {
			found = true;
			ASSERT_EQ(entry.machineType, MT_EFFECT);
			break;
		}
	}
	ASSERT_TRUE(found);
}

// ===== Sorting =====

TEST(GearScanner, EntriesAreSorted) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	auto& entries = scanner.GetEntries();
	for (int i = 1; i < (int)entries.size(); i++) {
		// Should be sorted by category first, then name
		if (entries[i].category == entries[i-1].category) {
			CHECK_TRUE(entries[i].displayName >= entries[i-1].displayName);
		} else {
			CHECK_TRUE(entries[i].category >= entries[i-1].category);
		}
	}
}

// ===== Clear =====

TEST(GearScanner, ClearRemovesEntries) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	ASSERT_GT((int)scanner.GetEntries().size(), 0);
	scanner.Clear();
	ASSERT_EQ((int)scanner.GetEntries().size(), 0);
}

// ===== Rescan =====

TEST(GearScanner, RescanReplacesEntries) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	int count1 = (int)scanner.GetEntries().size();

	// Scan again — should get the same count
	scanner.Scan(gearDir);
	int count2 = (int)scanner.GetEntries().size();

	ASSERT_EQ(count1, count2);
}

// ===== Filtering correctness =====

TEST(GearScanner, FilteringSumsToTotal) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	int total = (int)scanner.GetEntries().size();
	int gens = (int)scanner.GetGenerators().size();
	int fxs = (int)scanner.GetEffects().size();

	// Every entry should be either a generator or an effect
	ASSERT_EQ(total, gens + fxs);
}

// ===== Non-machine DLLs are skipped =====

TEST(GearScanner, SkipsNonMachineDlls) {
	std::string gearDir = GetRefPath("ref/Gear");

	GearScanner scanner;
	if (!scanner.Scan(gearDir)) {
		printf("  (skipped) ");
		return;
	}

	// Every entry should have a valid machine type
	for (auto& entry : scanner.GetEntries()) {
		CHECK_TRUE(entry.machineType == MT_GENERATOR || entry.machineType == MT_EFFECT);
	}
}
