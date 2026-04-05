#include <windows.h>
#include "TestFramework.h"
#include "../src/vst3/BuzzPluginView.h"
#include "../src/buzz/MachineInterface.h"

using namespace BuzzVst;
using namespace Steinberg;

// Helper to create a view without attaching it to a window.
// getSize() and setContentScaleFactor() work without a parent HWND.
static BuzzPluginView* MakeView() {
	return new BuzzPluginView("", "", "", {}, true);
}

// Base dimensions from the view (keep in sync with BuzzPluginView.h)
static const int BASE_W = 500;
static const int BASE_H = 900;

static int Scaled(int base, float factor) {
	return (int)(base * factor + 0.5f);
}

// ===== Default scale factor =====

TEST(HiDPI, DefaultSizeAt1x) {
	auto* view = MakeView();

	ViewRect size;
	tresult r = view->getSize(&size);
	ASSERT_EQ(r, kResultOk);

	ASSERT_EQ(size.left, 0);
	ASSERT_EQ(size.top, 0);
	ASSERT_EQ(size.right, BASE_W);
	ASSERT_EQ(size.bottom, BASE_H);

	view->release();
}

// ===== setContentScaleFactor changes reported size =====

TEST(HiDPI, SizeAt150Percent) {
	auto* view = MakeView();

	tresult r = view->setContentScaleFactor(1.5f);
	ASSERT_EQ(r, kResultTrue);

	ViewRect size;
	view->getSize(&size);

	ASSERT_EQ(size.right, Scaled(BASE_W, 1.5f));
	ASSERT_EQ(size.bottom, Scaled(BASE_H, 1.5f));

	view->release();
}

TEST(HiDPI, SizeAt200Percent) {
	auto* view = MakeView();

	view->setContentScaleFactor(2.0f);

	ViewRect size;
	view->getSize(&size);

	ASSERT_EQ(size.right, Scaled(BASE_W, 2.0f));
	ASSERT_EQ(size.bottom, Scaled(BASE_H, 2.0f));

	view->release();
}

TEST(HiDPI, SizeAt125Percent) {
	auto* view = MakeView();

	view->setContentScaleFactor(1.25f);

	ViewRect size;
	view->getSize(&size);

	ASSERT_EQ(size.right, Scaled(BASE_W, 1.25f));
	ASSERT_EQ(size.bottom, Scaled(BASE_H, 1.25f));

	view->release();
}

TEST(HiDPI, SizeAt100Percent) {
	auto* view = MakeView();

	// Explicitly set 1.0x
	view->setContentScaleFactor(1.0f);

	ViewRect size;
	view->getSize(&size);

	ASSERT_EQ(size.right, BASE_W);
	ASSERT_EQ(size.bottom, BASE_H);

	view->release();
}

// ===== Clamping =====

TEST(HiDPI, ScaleClampedToMinimum) {
	auto* view = MakeView();

	// Factor below 0.5 should be clamped to 0.5
	view->setContentScaleFactor(0.1f);

	ViewRect size;
	view->getSize(&size);

	ASSERT_EQ(size.right, Scaled(BASE_W, 0.5f));
	ASSERT_EQ(size.bottom, Scaled(BASE_H, 0.5f));

	view->release();
}

TEST(HiDPI, ScaleClampedToMaximum) {
	auto* view = MakeView();

	// Factor above 4.0 should be clamped to 4.0
	view->setContentScaleFactor(10.0f);

	ViewRect size;
	view->getSize(&size);

	ASSERT_EQ(size.right, Scaled(BASE_W, 4.0f));
	ASSERT_EQ(size.bottom, Scaled(BASE_H, 4.0f));

	view->release();
}

// ===== Repeated calls =====

TEST(HiDPI, SameFactorReturnsTrue) {
	auto* view = MakeView();

	view->setContentScaleFactor(1.5f);
	tresult r = view->setContentScaleFactor(1.5f);
	ASSERT_EQ(r, kResultTrue);

	view->release();
}

TEST(HiDPI, MultipleScaleChanges) {
	auto* view = MakeView();
	ViewRect size;

	view->setContentScaleFactor(1.0f);
	view->getSize(&size);
	ASSERT_EQ(size.right, BASE_W);

	view->setContentScaleFactor(2.0f);
	view->getSize(&size);
	ASSERT_EQ(size.right, Scaled(BASE_W, 2.0f));

	view->setContentScaleFactor(1.5f);
	view->getSize(&size);
	ASSERT_EQ(size.right, Scaled(BASE_W, 1.5f));

	// Back to 1x
	view->setContentScaleFactor(1.0f);
	view->getSize(&size);
	ASSERT_EQ(size.right, BASE_W);

	view->release();
}

// ===== getSize with null returns error =====

TEST(HiDPI, GetSizeNullReturnsError) {
	auto* view = MakeView();

	tresult r = view->getSize(nullptr);
	ASSERT_EQ(r, kInvalidArgument);

	view->release();
}

// ===== Platform type support =====

TEST(HiDPI, SupportsHWND) {
	auto* view = MakeView();

	ASSERT_EQ(view->isPlatformTypeSupported(kPlatformTypeHWND), kResultTrue);

	view->release();
}

TEST(HiDPI, RejectsNonHWND) {
	auto* view = MakeView();

	ASSERT_EQ(view->isPlatformTypeSupported(kPlatformTypeNSView), kResultFalse);

	view->release();
}

// ===== Interface query =====

TEST(HiDPI, QueryScaleInterface) {
	auto* view = MakeView();

	// Query for IPlugViewContentScaleSupport should succeed
	IPlugViewContentScaleSupport* scaleSupport = nullptr;
	tresult r = view->queryInterface(IPlugViewContentScaleSupport::iid, (void**)&scaleSupport);
	ASSERT_EQ(r, kResultOk);
	ASSERT_NOT_NULL(scaleSupport);

	if (scaleSupport) {
		// Use it
		scaleSupport->setContentScaleFactor(2.0f);
		ViewRect size;
		view->getSize(&size);
		ASSERT_EQ(size.right, Scaled(BASE_W, 2.0f));

		scaleSupport->release();
	}

	view->release();
}

TEST(HiDPI, QueryPlugViewInterface) {
	auto* view = MakeView();

	IPlugView* plugView = nullptr;
	tresult r = view->queryInterface(IPlugView::iid, (void**)&plugView);
	ASSERT_EQ(r, kResultOk);
	ASSERT_NOT_NULL(plugView);

	if (plugView)
		plugView->release();

	view->release();
}

// ===== Fractional scaling precision =====

TEST(HiDPI, FractionalScaling175) {
	auto* view = MakeView();

	view->setContentScaleFactor(1.75f);

	ViewRect size;
	view->getSize(&size);

	ASSERT_EQ(size.right, Scaled(BASE_W, 1.75f));
	ASSERT_EQ(size.bottom, Scaled(BASE_H, 1.75f));

	view->release();
}

TEST(HiDPI, FractionalScaling250) {
	auto* view = MakeView();

	view->setContentScaleFactor(2.5f);

	ViewRect size;
	view->getSize(&size);

	ASSERT_EQ(size.right, Scaled(BASE_W, 2.5f));
	ASSERT_EQ(size.bottom, Scaled(BASE_H, 2.5f));

	view->release();
}

// ===== Aspect ratio preserved =====

TEST(HiDPI, AspectRatioPreserved) {
	auto* view = MakeView();

	// Check that width/height ratio is the same at all scales
	float baseRatio = (float)BASE_W / (float)BASE_H;

	float scales[] = { 1.0f, 1.25f, 1.5f, 2.0f, 3.0f };
	for (float s : scales) {
		view->setContentScaleFactor(s);
		ViewRect size;
		view->getSize(&size);

		float ratio = (float)size.right / (float)size.bottom;
		// Allow small rounding error
		float diff = ratio - baseRatio;
		if (diff < 0) diff = -diff;
		CHECK_TRUE(diff < 0.02f);
	}

	view->release();
}
