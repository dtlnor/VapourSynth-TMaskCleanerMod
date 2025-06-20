#include "shared.h"

static void VS_CC FilterFree(void* instanceData, VSCore* core, const VSAPI* vsapi) {
	auto d{ static_cast<TMCData*>(instanceData) };
	vsapi->freeNode(d->node);
	delete d;
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi) {
	vspapi->configPlugin("com.dtlnor.tmcm", "tmcm", "A really simple mask cleaning plugin for VapourSynth based on mt_hysteresis.", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
	vspapi->registerFunction("TMaskCleanerMod",
		"clip:vnode;"
		"length:int:opt;"
		"thresh:float:opt;"
		"fade:int:opt;"
		"binarize:int:opt;"
		"connectivity:int:opt;"
		"reverse:int:opt;"
		"mode:int:opt;",
		"clip:vnode;",
		TMCCreate, nullptr, plugin);

	vspapi->registerFunction("GetCCLStats",
		"clip:vnode;"
		"thresh:float:opt;"
		"connectivity:int:opt;",
		"clip:vnode;",
		CCLSCreate, nullptr, plugin);
}
