#include "gtest/gtest.h"
#include "QuickJSRuntime.h"
#include "jsi/test/testlib.h"

namespace facebook::jsi {

std::vector<RuntimeFactory> runtimeGenerators()
{
  return { RuntimeFactory([]() -> std::unique_ptr<Runtime>
  {
    quickjs::QuickJSRuntimeArgs args;
    return quickjs::makeQuickJSRuntime(std::move(args));
  }) };
}

} // namespace facebook::jsi

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
