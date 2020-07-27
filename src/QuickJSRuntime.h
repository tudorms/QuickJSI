#pragma once
#include <jsi/jsi.h>

namespace quickjs {

struct QuickJSRuntimeArgs
{
	bool enableTracing { false };
};

std::unique_ptr<facebook::jsi::Runtime> __cdecl makeQuickJSRuntime(QuickJSRuntimeArgs&& args);

}
