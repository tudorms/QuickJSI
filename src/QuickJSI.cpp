#include <iostream>
#include <quickjs-libc.h>
#include <QuickJSRuntime.h>
#include "gtest/gtest.h"

TEST(Basic, SimpleTest)
{
    quickjs::QuickJSRuntimeArgs args;
    auto runtime = quickjs::makeQuickJSRuntime(std::move(args));

    runtime->evaluateJavaScript(std::make_unique<facebook::jsi::StringBuffer>(
        "let x = 2;" "\n"
        "var result = `result is ${x + x}`;" "\n"
        ), "<test_code>");

    auto result = runtime->global().getProperty(*runtime, "result");

    EXPECT_EQ(result.getString(*runtime).utf8(*runtime), "result is 4");
}
