#include <iostream>
#include <jsi/jsi.h>
#include <quickjs-libc.h>
#include "gtest/gtest.h"

TEST(SimpleTest)
{
  JSRuntime* rt = JS_NewRuntime();

  js_std_init_handlers(rt);

  JSContext* ctx = JS_NewContext(rt);

  //js_std_loop(ctx);

  std::string code = "let x = 2; x + x;";

  JSValue val = JS_Eval(ctx, code.c_str(), code.size(), "filename", 0);

  EXPECT_TRUE(JS_VALUE_GET_TAG(val) == JS_TAG_INT);
  EXPECT_TRUE(JS_VALUE_GET_INT(val) == 4);

  JS_FreeValue(ctx, val);

  js_std_free_handlers(rt);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
}
