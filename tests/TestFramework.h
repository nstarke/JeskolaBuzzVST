#pragma once

// Lightweight test framework for BuzzBridge.
// Supports test registration, assertions, and summary reporting.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <functional>

#include "common/SEHGuard.h"

namespace TestFW {

struct TestCase {
	const char* suite;
	const char* name;
	std::function<void()> func;
};

struct TestResult {
	int passed = 0;
	int failed = 0;
	int total = 0;
	std::vector<std::string> failures;
};

inline std::vector<TestCase>& GetTests() {
	static std::vector<TestCase> tests;
	return tests;
}

inline TestResult& GetResult() {
	static TestResult result;
	return result;
}

inline int RegisterTest(const char* suite, const char* name, std::function<void()> func) {
	GetTests().push_back({suite, name, std::move(func)});
	return 0;
}

inline void ReportFailure(const char* file, int line, const char* expr) {
	char buf[512];
	snprintf(buf, sizeof(buf), "  FAIL: %s:%d: %s", file, line, expr);
	GetResult().failures.push_back(buf);
	fprintf(stderr, "%s\n", buf);
}

// SEH wrapper must be in a function that has no C++ objects needing unwinding.
// We use a raw function pointer to avoid std::function on the stack.
typedef void (*RawTestFunc)();

inline bool RunTestWithSEH(RawTestFunc fn) {
	// Uses real SEH on MSVC, a VEH+longjmp guard under clang-mingw/WINE, and a
	// passthrough elsewhere — so a crashing test is reported as CRASH instead
	// of taking down the whole runner. See src/common/SEHGuard.h.
	return BuzzVst::SEH_Call([&]() { fn(); });
}

inline int RunAllTests() {
	auto& tests = GetTests();
	auto& result = GetResult();
	result = {};

	printf("Running %d test(s)...\n\n", (int)tests.size());

	for (auto& tc : tests) {
		result.total++;
		int failsBefore = (int)result.failures.size();

		printf("[%s.%s] ", tc.suite, tc.name);
		fflush(stdout);

		// Store the current test function in a static so the SEH wrapper can call it
		static std::function<void()>* currentFunc = nullptr;
		currentFunc = &tc.func;

		// Use a raw lambda that doesn't capture (converts to function pointer)
		bool ok = RunTestWithSEH([]() { (*currentFunc)(); });

		if (!ok) {
			char buf[256];
			snprintf(buf, sizeof(buf), "  CRASH in %s.%s", tc.suite, tc.name);
			result.failures.push_back(buf);
			printf("CRASH\n");
			result.failed++;
		} else if ((int)result.failures.size() > failsBefore) {
			printf("FAILED\n");
			result.failed++;
		} else {
			printf("OK\n");
			result.passed++;
		}
	}

	printf("\n========================================\n");
	printf("Results: %d passed, %d failed, %d total\n",
	       result.passed, result.failed, result.total);

	if (!result.failures.empty()) {
		printf("\nFailures:\n");
		for (auto& f : result.failures) {
			printf("%s\n", f.c_str());
		}
	}

	printf("========================================\n");
	return result.failed > 0 ? 1 : 0;
}

} // namespace TestFW

// Macros for defining and registering tests
#define TEST(Suite, Name)                                                    \
	static void Test_##Suite##_##Name();                                     \
	static int _reg_##Suite##_##Name =                                       \
		TestFW::RegisterTest(#Suite, #Name, Test_##Suite##_##Name);           \
	static void Test_##Suite##_##Name()

// Assertion macros
#define ASSERT_TRUE(expr)                                                    \
	do { if (!(expr)) {                                                      \
		TestFW::ReportFailure(__FILE__, __LINE__, "ASSERT_TRUE(" #expr ")"); \
		return;                                                              \
	}} while(0)

#define ASSERT_FALSE(expr)                                                   \
	do { if ((expr)) {                                                       \
		TestFW::ReportFailure(__FILE__, __LINE__, "ASSERT_FALSE(" #expr ")"); \
		return;                                                              \
	}} while(0)

#define ASSERT_EQ(a, b)                                                      \
	do { if ((a) != (b)) {                                                   \
		TestFW::ReportFailure(__FILE__, __LINE__, "ASSERT_EQ(" #a ", " #b ")"); \
		return;                                                              \
	}} while(0)

#define ASSERT_NE(a, b)                                                      \
	do { if ((a) == (b)) {                                                   \
		TestFW::ReportFailure(__FILE__, __LINE__, "ASSERT_NE(" #a ", " #b ")"); \
		return;                                                              \
	}} while(0)

#define ASSERT_LT(a, b)                                                      \
	do { if (!((a) < (b))) {                                                 \
		TestFW::ReportFailure(__FILE__, __LINE__, "ASSERT_LT(" #a ", " #b ")"); \
		return;                                                              \
	}} while(0)

#define ASSERT_LE(a, b)                                                      \
	do { if (!((a) <= (b))) {                                                \
		TestFW::ReportFailure(__FILE__, __LINE__, "ASSERT_LE(" #a ", " #b ")"); \
		return;                                                              \
	}} while(0)

#define ASSERT_GT(a, b)                                                      \
	do { if (!((a) > (b))) {                                                 \
		TestFW::ReportFailure(__FILE__, __LINE__, "ASSERT_GT(" #a ", " #b ")"); \
		return;                                                              \
	}} while(0)

#define ASSERT_GE(a, b)                                                      \
	do { if (!((a) >= (b))) {                                                \
		TestFW::ReportFailure(__FILE__, __LINE__, "ASSERT_GE(" #a ", " #b ")"); \
		return;                                                              \
	}} while(0)

#define ASSERT_NEAR(a, b, epsilon)                                           \
	do { if (fabs((double)(a) - (double)(b)) > (double)(epsilon)) {          \
		TestFW::ReportFailure(__FILE__, __LINE__,                            \
			"ASSERT_NEAR(" #a ", " #b ", " #epsilon ")");                    \
		return;                                                              \
	}} while(0)

#define ASSERT_NOT_NULL(ptr)                                                 \
	do { if ((ptr) == nullptr) {                                             \
		TestFW::ReportFailure(__FILE__, __LINE__, "ASSERT_NOT_NULL(" #ptr ")"); \
		return;                                                              \
	}} while(0)

#define ASSERT_NULL(ptr)                                                     \
	do { if ((ptr) != nullptr) {                                             \
		TestFW::ReportFailure(__FILE__, __LINE__, "ASSERT_NULL(" #ptr ")");   \
		return;                                                              \
	}} while(0)

// Non-fatal check variants (continue after failure)
#define CHECK_TRUE(expr)                                                     \
	do { if (!(expr)) {                                                      \
		TestFW::ReportFailure(__FILE__, __LINE__, "CHECK_TRUE(" #expr ")");  \
	}} while(0)

#define CHECK_EQ(a, b)                                                       \
	do { if ((a) != (b)) {                                                   \
		TestFW::ReportFailure(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ")"); \
	}} while(0)

#define CHECK_NE(a, b)                                                       \
	do { if ((a) == (b)) {                                                   \
		TestFW::ReportFailure(__FILE__, __LINE__, "CHECK_NE(" #a ", " #b ")"); \
	}} while(0)
