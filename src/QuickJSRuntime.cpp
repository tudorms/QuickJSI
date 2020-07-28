#include <memory>

#include <quickjs-libc.h>

#include "QuickJSRuntime.h"

using namespace facebook::jsi;

namespace quickjs {

class QuickJSRuntime : public facebook::jsi::Runtime
{
private:
    JSRuntime* _rt;
    JSContext* _ctx;

    class QuickJSStringValue final : public Runtime::PointerValue
    {
        QuickJSStringValue(JSContext* ctx, JSValue val) :
            _ctx(ctx), _val(JS_DupValue(ctx, val))
        {
            size_t plen;
            _ptr = JS_ToCStringLen(_ctx, &plen, val);
            //if (!ptr) throw JSError(*this, "OOM");
        }

        void invalidate() override
        {
            JS_FreeCString(_ctx, _ptr);
            _ptr = nullptr;

            JS_FreeValue(_ctx, _val);

            delete this;
        }

    private:
        JSContext* _ctx;
        JSValue _val;
        const char* _ptr;

    protected:
        friend class QuickJSRuntime;
    };

    String createString(JSValue val)
    {
        return make<String>(new QuickJSStringValue(_ctx, val));
    }

    Value createValue(JSValue val)
    {
        switch (JS_VALUE_GET_TAG(val))
        {
            case JS_TAG_INT:
                return Value(JS_VALUE_GET_INT(val));

            case JS_TAG_FLOAT64:
                return Value(JS_VALUE_GET_FLOAT64(val));

            case JS_TAG_BOOL:
                return Value(JS_VALUE_GET_BOOL(val));

            case JS_TAG_UNDEFINED:
                return Value();

            case JS_TAG_NULL:
            case JS_TAG_UNINITIALIZED:
                return Value(nullptr);

            case JS_TAG_STRING:
                return createString(val);

            // TODO: rest of types

            default:
                return Value();
        }
    }

public:
    QuickJSRuntime(QuickJSRuntimeArgs&& args)
    {
        _rt = JS_NewRuntime();
        js_std_init_handlers(_rt);
        _ctx = JS_NewContext(_rt);
    }

    ~QuickJSRuntime()
    {
        js_std_free_handlers(_rt);
        JS_FreeContext(_ctx);
        JS_FreeRuntime(_rt);
    }

    virtual Value evaluateJavaScript(const std::shared_ptr<const Buffer>& buffer, const std::string& sourceURL) override
    {
        JSValue val = JS_Eval(_ctx, reinterpret_cast<const char*>(buffer->data()), buffer->size(), sourceURL.c_str(), 0);

        auto result = createValue(val);

        JS_FreeValue(_ctx, val);

        return result;
    }

    virtual std::shared_ptr<const PreparedJavaScript> prepareJavaScript(const std::shared_ptr<const Buffer>& buffer, std::string sourceURL) override
    {
        return std::shared_ptr<const PreparedJavaScript>();
    }

    virtual Value evaluatePreparedJavaScript(const std::shared_ptr<const PreparedJavaScript>& js) override
    {
        return Value();
    }
    virtual Object global() override
    {
        return make<Object>(nullptr);
    }
    virtual std::string description() override
    {
        return std::string();
    }
    virtual bool isInspectable() override
    {
        return false;
    }
    virtual PointerValue* cloneSymbol(const Runtime::PointerValue* pv) override
    {
        return nullptr;
    }
    virtual PointerValue* cloneString(const Runtime::PointerValue* pv) override
    {
        return new QuickJSStringValue(_ctx, JS_DupValue(_ctx, static_cast<const QuickJSStringValue*>(pv)->_val));
    }
    virtual PointerValue* cloneObject(const Runtime::PointerValue* pv) override
    {
        return nullptr;
    }
    virtual PointerValue* clonePropNameID(const Runtime::PointerValue* pv) override
    {
        return nullptr;
    }
    virtual PropNameID createPropNameIDFromAscii(const char* str, size_t length) override
    {
        return make<PropNameID>(nullptr);
    }
    virtual PropNameID createPropNameIDFromUtf8(const uint8_t* utf8, size_t length) override
    {
        return make<PropNameID>(nullptr);
    }
    virtual PropNameID createPropNameIDFromString(const String& str) override
    {
        return make<PropNameID>(nullptr);
    }
    virtual std::string utf8(const PropNameID&) override
    {
        return std::string();
    }
    virtual bool compare(const PropNameID&, const PropNameID&) override
    {
        return false;
    }
    virtual std::string symbolToString(const Symbol&) override
    {
        return std::string();
    }
    virtual String createStringFromAscii(const char* str, size_t length) override
    {
        return make<String>(nullptr);
    }
    virtual String createStringFromUtf8(const uint8_t* utf8, size_t length) override
    {
        return make<String>(nullptr);
    }
    virtual std::string utf8(const String& str) override
    {
        const QuickJSStringValue* qjsStringValue =
            static_cast<const QuickJSStringValue*>(getPointerValue(str));

        return std::string(qjsStringValue->_ptr);
    }
    virtual Object createObject() override
    {
        return make<Object>(nullptr);
    }
    virtual Object createObject(std::shared_ptr<HostObject> ho) override
    {
        return make<Object>(nullptr);
    }
    virtual std::shared_ptr<HostObject> getHostObject(const Object&) override
    {
        return std::shared_ptr<HostObject>();
    }
    virtual HostFunctionType& getHostFunction(const Function&) override
    {
        std::abort();
    }
    virtual Value getProperty(const Object&, const PropNameID& name) override
    {
        return Value();
    }
    virtual Value getProperty(const Object&, const String& name) override
    {
        return Value();
    }
    virtual bool hasProperty(const Object&, const PropNameID& name) override
    {
        return false;
    }
    virtual bool hasProperty(const Object&, const String& name) override
    {
        return false;
    }
    virtual void setPropertyValue(Object&, const PropNameID& name, const Value& value) override
    {
    }
    virtual void setPropertyValue(Object&, const String& name, const Value& value) override
    {
    }
    virtual bool isArray(const Object&) const override
    {
        return false;
    }
    virtual bool isArrayBuffer(const Object&) const override
    {
        return false;
    }
    virtual bool isFunction(const Object&) const override
    {
        return false;
    }
    virtual bool isHostObject(const Object&) const override
    {
        return false;
    }
    virtual bool isHostFunction(const Function&) const override
    {
        return false;
    }
    virtual Array getPropertyNames(const Object&) override
    {
        return createObject().getArray(*this);
    }
    virtual WeakObject createWeakObject(const Object&) override
    {
        return make<WeakObject>(nullptr);
    }
    virtual Value lockWeakObject(const WeakObject&) override
    {
        return Value();
    }
    virtual Array createArray(size_t length) override
    {
        return createObject().getArray(*this);
    }
    virtual size_t size(const Array&) override
    {
        return size_t();
    }
    virtual size_t size(const ArrayBuffer&) override
    {
        return size_t();
    }
    virtual uint8_t* data(const ArrayBuffer&) override
    {
        return nullptr;
    }
    virtual Value getValueAtIndex(const Array&, size_t i) override
    {
        return Value();
    }
    virtual void setValueAtIndexImpl(Array&, size_t i, const Value& value) override
    {
    }
    virtual Function createFunctionFromHostFunction(const PropNameID& name, unsigned int paramCount, HostFunctionType func) override
    {
        return createObject().getFunction(*this);
    }
    virtual Value call(const Function&, const Value& jsThis, const Value* args, size_t count) override
    {
        return Value();
    }
    virtual Value callAsConstructor(const Function&, const Value* args, size_t count) override
    {
        return Value();
    }
    virtual bool strictEquals(const Symbol& a, const Symbol& b) const override
    {
        return false;
    }
    virtual bool strictEquals(const String& a, const String& b) const override
    {
        return false;
    }
    virtual bool strictEquals(const Object& a, const Object& b) const override
    {
        return false;
    }
    virtual bool instanceOf(const Object& o, const Function& f) override
    {
        return false;
    }
};

std::unique_ptr<Runtime> __cdecl makeQuickJSRuntime(QuickJSRuntimeArgs&& args)
{
    return std::make_unique<QuickJSRuntime>(std::move(args));
}

}
