#include <windows.h>
#include "TestFramework.h"
#include "../src/vst3/BuzzPluginView.h"
#include "../src/buzz/MachineInterface.h"

using namespace BuzzVst;
using namespace Steinberg;
using namespace Steinberg::Vst;

// Helper to create a detached view (no parent HWND)
static BuzzPluginView* MakeView() {
	return new BuzzPluginView("", "", "", {}, true);
}

static std::vector<ParamViewInfo> MakeTestParams(int count) {
	std::vector<ParamViewInfo> params;
	for (int i = 0; i < count; i++) {
		ParamViewInfo pvi;
		pvi.paramId = 100 + i;
		pvi.name = "Param " + std::to_string(i);
		pvi.stepCount = 128;
		pvi.normalizedValue = (double)i / (double)(count > 1 ? count - 1 : 1);
		params.push_back(pvi);
	}
	return params;
}

// ===== ParamViewInfo struct =====

TEST(ParamSliders, ParamViewInfoDefault) {
	ParamViewInfo pvi;
	pvi.paramId = 42;
	pvi.name = "TestParam";
	pvi.stepCount = 255;
	pvi.normalizedValue = 0.5;

	ASSERT_EQ((int)pvi.paramId, 42);
	ASSERT_EQ(pvi.name, "TestParam");
	ASSERT_EQ(pvi.stepCount, 255);
	ASSERT_TRUE(pvi.normalizedValue > 0.49 && pvi.normalizedValue < 0.51);
}

// ===== setParamInfo stores data =====

TEST(ParamSliders, SetParamInfoEmpty) {
	auto* view = MakeView();
	std::vector<ParamViewInfo> empty;
	view->setParamInfo(empty);
	// Should not crash — no params, no controls
	view->release();
}

TEST(ParamSliders, SetParamInfoMultiple) {
	auto* view = MakeView();
	auto params = MakeTestParams(5);
	view->setParamInfo(params);
	// No crash with multiple params
	view->release();
}

TEST(ParamSliders, SetParamInfoReplace) {
	auto* view = MakeView();

	auto params1 = MakeTestParams(3);
	view->setParamInfo(params1);

	auto params2 = MakeTestParams(7);
	view->setParamInfo(params2);

	// Replacing params should not crash or leak
	view->release();
}

TEST(ParamSliders, SetParamInfoLargeCount) {
	auto* view = MakeView();
	auto params = MakeTestParams(50);
	view->setParamInfo(params);
	// 50 params should work (scrollable)
	view->release();
}

// ===== updateParamValue =====

TEST(ParamSliders, UpdateParamValueNoParams) {
	auto* view = MakeView();
	// Should not crash even with no params set
	view->updateParamValue(42, 0.5);
	view->release();
}

TEST(ParamSliders, UpdateParamValueWithParams) {
	auto* view = MakeView();
	auto params = MakeTestParams(3);
	view->setParamInfo(params);

	// Update each param
	view->updateParamValue(100, 0.0);
	view->updateParamValue(101, 0.5);
	view->updateParamValue(102, 1.0);

	// Should not crash
	view->release();
}

TEST(ParamSliders, UpdateParamValueUnknownId) {
	auto* view = MakeView();
	auto params = MakeTestParams(3);
	view->setParamInfo(params);

	// Unknown param ID should not crash
	view->updateParamValue(9999, 0.5);
	view->release();
}

// ===== Callbacks =====

TEST(ParamSliders, CallbacksInitiallyNull) {
	auto* view = MakeView();
	// Callbacks should be null/empty by default
	ASSERT_TRUE(!view->onParamBeginEdit);
	ASSERT_TRUE(!view->onParamChanged);
	ASSERT_TRUE(!view->onParamEndEdit);
	view->release();
}

TEST(ParamSliders, CallbacksCanBeSet) {
	auto* view = MakeView();

	bool beginCalled = false;
	bool changeCalled = false;
	bool endCalled = false;
	ParamID lastId = 0;
	double lastValue = 0;

	view->onParamBeginEdit = [&](ParamID id) {
		beginCalled = true;
		lastId = id;
	};
	view->onParamChanged = [&](ParamID id, double value) {
		changeCalled = true;
		lastId = id;
		lastValue = value;
	};
	view->onParamEndEdit = [&](ParamID id) {
		endCalled = true;
		lastId = id;
	};

	// Verify callbacks are set
	ASSERT_TRUE(!!view->onParamBeginEdit);
	ASSERT_TRUE(!!view->onParamChanged);
	ASSERT_TRUE(!!view->onParamEndEdit);

	// Call them directly to verify wiring
	view->onParamBeginEdit(42);
	ASSERT_TRUE(beginCalled);
	ASSERT_EQ((int)lastId, 42);

	view->onParamChanged(43, 0.75);
	ASSERT_TRUE(changeCalled);
	ASSERT_EQ((int)lastId, 43);
	ASSERT_TRUE(lastValue > 0.74 && lastValue < 0.76);

	view->onParamEndEdit(44);
	ASSERT_TRUE(endCalled);
	ASSERT_EQ((int)lastId, 44);

	view->release();
}

// ===== Param info with various step counts =====

TEST(ParamSliders, ContinuousParam) {
	auto* view = MakeView();
	std::vector<ParamViewInfo> params;
	ParamViewInfo pvi;
	pvi.paramId = 10;
	pvi.name = "Continuous";
	pvi.stepCount = 0; // continuous
	pvi.normalizedValue = 0.5;
	params.push_back(pvi);
	view->setParamInfo(params);
	view->release();
}

TEST(ParamSliders, DiscreteParam) {
	auto* view = MakeView();
	std::vector<ParamViewInfo> params;
	ParamViewInfo pvi;
	pvi.paramId = 10;
	pvi.name = "Discrete";
	pvi.stepCount = 3; // 4 positions: 0, 1, 2, 3
	pvi.normalizedValue = 0.33;
	params.push_back(pvi);
	view->setParamInfo(params);
	view->release();
}

TEST(ParamSliders, SwitchParam) {
	auto* view = MakeView();
	std::vector<ParamViewInfo> params;
	ParamViewInfo pvi;
	pvi.paramId = 10;
	pvi.name = "Switch";
	pvi.stepCount = 1; // on/off
	pvi.normalizedValue = 1.0;
	params.push_back(pvi);
	view->setParamInfo(params);
	view->release();
}

// ===== Edge cases =====

TEST(ParamSliders, SetParamInfoThenClear) {
	auto* view = MakeView();
	view->setParamInfo(MakeTestParams(10));
	view->setParamInfo({}); // clear all
	view->release();
}

TEST(ParamSliders, UpdateBeforeSetParamInfo) {
	auto* view = MakeView();
	// Update before any params set — should not crash
	view->updateParamValue(100, 0.5);
	view->setParamInfo(MakeTestParams(3));
	view->updateParamValue(100, 0.75);
	view->release();
}

TEST(ParamSliders, NormalizedValueBounds) {
	auto* view = MakeView();
	auto params = MakeTestParams(1);
	view->setParamInfo(params);

	// Test boundary values
	view->updateParamValue(100, 0.0);
	view->updateParamValue(100, 1.0);
	view->updateParamValue(100, -0.1); // below range
	view->updateParamValue(100, 1.5);  // above range

	view->release();
}
