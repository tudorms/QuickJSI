#include <iostream>
#include <quickjs-libc.h>
#include <QuickJSRuntime.h>
#include "gtest/gtest.h"

TEST(SimpleTest)
{
    quickjs::QuickJSRuntimeArgs args;
    auto runtime = quickjs::makeQuickJSRuntime(std::move(args));

    auto val = runtime->evaluateJavaScript(std::make_unique<facebook::jsi::StringBuffer>("let x = 2; x + x;"), "");
}
