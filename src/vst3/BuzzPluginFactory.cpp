#include "GeneratorProcessor.h"
#include "EffectProcessor.h"
#include "BuzzController.h"
#include "plugids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg::Vst;
using namespace BuzzVst;

//------------------------------------------------------------------------
//  VST Plug-in Entry
//------------------------------------------------------------------------

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

	//--- Buzz Generator (Instrument) ---
	DEF_CLASS2(
		INLINE_UID_FROM_FUID(kBuzzGeneratorProcessorUID),
		PClassInfo::kManyInstances,
		kVstAudioEffectClass,
		"Buzz Generator Bridge",
		Vst::kDistributable,
		Vst::PlugType::kInstrumentSynth,
		BUZZVST_VERSION_STR,
		kVstVersionString,
		BuzzVst::GeneratorProcessor::createInstance
	)

	DEF_CLASS2(
		INLINE_UID_FROM_FUID(kBuzzGeneratorControllerUID),
		PClassInfo::kManyInstances,
		kVstComponentControllerClass,
		"Buzz Generator Bridge Controller",
		0,
		"",
		BUZZVST_VERSION_STR,
		kVstVersionString,
		BuzzVst::GeneratorController::createInstance
	)

	//--- Buzz Effect ---
	DEF_CLASS2(
		INLINE_UID_FROM_FUID(kBuzzEffectProcessorUID),
		PClassInfo::kManyInstances,
		kVstAudioEffectClass,
		"Buzz Effect Bridge",
		Vst::kDistributable,
		Vst::PlugType::kFx,
		BUZZVST_VERSION_STR,
		kVstVersionString,
		BuzzVst::EffectProcessor::createInstance
	)

	DEF_CLASS2(
		INLINE_UID_FROM_FUID(kBuzzEffectControllerUID),
		PClassInfo::kManyInstances,
		kVstComponentControllerClass,
		"Buzz Effect Bridge Controller",
		0,
		"",
		BUZZVST_VERSION_STR,
		kVstVersionString,
		BuzzVst::EffectController::createInstance
	)

END_FACTORY
