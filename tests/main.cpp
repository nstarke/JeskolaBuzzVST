#include <windows.h>
#include "TestFramework.h"
#include "../src/common/PatchMessageBoxes.h"

int main(int argc, char* argv[])
{
	BuzzVst::PatchMessageBoxes();

	printf("BuzzBridge Test Suite\n");
	printf("========================================\n\n");

	return TestFW::RunAllTests();
}
