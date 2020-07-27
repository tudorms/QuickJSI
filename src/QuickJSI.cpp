#include <iostream>
#include <jsi/jsi.h>
#include <quickjs-libc.h>

int main()
{
    JSRuntime* rt = JS_NewRuntime();

    js_std_init_handlers(rt);

    JSContext*  ctx = JS_NewContext(rt);

    //js_std_loop(ctx);

    std::string code = "let x = 2; x + x;";

    JSValue val = JS_Eval(ctx, code.c_str(), code.size(), "filename", 0);

    JS_FreeValue(ctx, val);

    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    std::cout << "Hello World!\n";
}
