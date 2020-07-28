#include <memory>

#include <quickjs-libc.h>
#include <quickjspp.hpp>

#include "QuickJSRuntime.h"

using namespace facebook::jsi;

namespace quickjs {

class QuickJSRuntime : public facebook::jsi::Runtime
{
private:
    qjs::Runtime _runtime;
    qjs::Context _context;

    class QuickJSPointerValue final : public Runtime::PointerValue
    {
        QuickJSPointerValue(qjs::Value&& val) :
            _val(std::move(val))
        {
        }

        QuickJSPointerValue(const qjs::Value& val) :
            _val(val)
        {
        }

        void invalidate() override
        {
            delete this;
        }

    private:
        qjs::Value _val;

    protected:
        friend class QuickJSRuntime;
    };

    String createString(qjs::Value&& val)
    {
        return make<String>(new QuickJSPointerValue(std::move(val)));
    }

    Object createObject(qjs::Value&& val)
    {
        return make<Object>(new QuickJSPointerValue(std::move(val)));
    }

    String throwException(qjs::Value&& val)
    {
        // TODO:
        return make<String>(new QuickJSPointerValue(_context.getException()));
    }

    Value createValue(qjs::Value&& val)
    {
        switch (val.getTag())
        {
            case JS_TAG_INT:
                return Value(static_cast<int>(val.as<int64_t>()));

            case JS_TAG_FLOAT64:
                return Value(val.as<double>());

            case JS_TAG_BOOL:
                return Value(val.as<bool>());

            case JS_TAG_UNDEFINED:
                return Value();

            case JS_TAG_NULL:
            case JS_TAG_UNINITIALIZED:
                return Value(nullptr);

            case JS_TAG_STRING:
                return createString(std::move(val));

            case JS_TAG_OBJECT:
                return createObject(std::move(val));

            case JS_TAG_EXCEPTION:
                return throwException(std::move(val));

            // TODO: rest of types
            case JS_TAG_BIG_DECIMAL:
            case JS_TAG_BIG_INT:
            case JS_TAG_BIG_FLOAT:
            case JS_TAG_SYMBOL:
            case JS_TAG_CATCH_OFFSET:
            default:
                return Value();
        }
    }

public:
    QuickJSRuntime(QuickJSRuntimeArgs&& args) :
        _runtime(), _context(_runtime)
    {
    }

    ~QuickJSRuntime()
    {
    }

    virtual Value evaluateJavaScript(const std::shared_ptr<const Buffer>& buffer, const std::string& sourceURL) override
    {
        auto val = _context.eval(reinterpret_cast<const char*>(buffer->data()), sourceURL.c_str());
        auto result = createValue(std::move(val));
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
        return createObject(_context.global());
    }
    virtual std::string description() override
    {
        return "QuickJS";
    }
    virtual bool isInspectable() override
    {
        return false;
    }
    virtual PointerValue* cloneSymbol(const Runtime::PointerValue* pv) override
    {
        return new QuickJSPointerValue(static_cast<const QuickJSPointerValue*>(pv)->_val);
    }
    virtual PointerValue* cloneString(const Runtime::PointerValue* pv) override
    {
        // TODO: validate this calls the copy constructor
        return new QuickJSPointerValue(static_cast<const QuickJSPointerValue*>(pv)->_val);
    }
    virtual PointerValue* cloneObject(const Runtime::PointerValue* pv) override
    {
        return new QuickJSPointerValue(static_cast<const QuickJSPointerValue*>(pv)->_val);
    }
    virtual PointerValue* clonePropNameID(const Runtime::PointerValue* pv) override
    {
        return new QuickJSPointerValue(static_cast<const QuickJSPointerValue*>(pv)->_val);
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
        const QuickJSPointerValue* qjsStringValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(str));

        return qjsStringValue->_val.as<std::string>();
    }
    virtual Object createObject() override
    {
        return createObject(_context.newObject());
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
    virtual bool isArray(const Object& obj) const override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(obj));

        return qjsObjectValue->_val.isArray();
    }
    virtual bool isArrayBuffer(const Object&) const override
    {
        return false;
    }
    virtual bool isFunction(const Object& obj) const override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(obj));

        return qjsObjectValue->_val.isFunction();
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
        return createObject(qjs::Value { _context.ctx, JS_NewArray(_context.ctx) }).getArray(*this);
    }
    virtual size_t size(const Array& arr) override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(arr));

        return static_cast<int32_t>(qjsObjectValue->_val["length"]);
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
