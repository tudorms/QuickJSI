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

    PropNameID createPropNameID(qjs::Value&& val)
    {
        return make<PropNameID>(new QuickJSPointerValue(std::move(val)));
    }

    PropNameID createPropNameID(const qjs::Value& val)
    {
        return make<PropNameID>(new QuickJSPointerValue(val));
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

    qjs::Value fromJSIValue(const Value& value)
    {
        if (value.isUndefined())
        {
            return qjs::Value { nullptr, JS_UNDEFINED };
        }
        else if (value.isNull())
        {
            return qjs::Value { nullptr, JS_NULL };
        }
        else if (value.isBool())
        {
            return _context.newValue(value.getBool());
        }
        else if (value.isNumber())
        {
            return _context.newValue(value.getNumber());
        }
        else if (value.isString())
        {
            // TODO: Validate me!
            const QuickJSPointerValue* qjsObjectValue =
                static_cast<const QuickJSPointerValue*>(getPointerValue(value.asString(*this)));
            return qjsObjectValue->_val;
        }
        else if (value.isObject())
        {
            // TODO: Validate me!
            const QuickJSPointerValue* qjsObjectValue =
                static_cast<const QuickJSPointerValue*>(getPointerValue(value.getObject(*this)));
            return qjsObjectValue->_val;
        }
        else
        {
            std::abort();
        }
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
        auto val = _context.eval(reinterpret_cast<const char*>(buffer->data()), sourceURL.c_str(), JS_EVAL_TYPE_GLOBAL);
        auto result = createValue(std::move(val));
        return result;
    }

    virtual std::shared_ptr<const PreparedJavaScript> prepareJavaScript(const std::shared_ptr<const Buffer>& buffer, std::string sourceURL) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual Value evaluatePreparedJavaScript(const std::shared_ptr<const PreparedJavaScript>& js) override
    {
        // TODO: NYI
        std::abort();
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
        return createPropNameID(_context.newValue(std::string_view { str, length }));
    }

    virtual PropNameID createPropNameIDFromUtf8(const uint8_t* utf8, size_t length) override
    {
        return createPropNameID(_context.newValue(std::string_view { (const char*)utf8, length }));
    }

    virtual PropNameID createPropNameIDFromString(const String& str) override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(str));

        return createPropNameID(qjsObjectValue->_val);
    }

    virtual std::string utf8(const PropNameID& sym) override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(sym));

        return qjsObjectValue->_val.as<std::string>();
    }

    virtual bool compare(const PropNameID&, const PropNameID&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual std::string symbolToString(const Symbol&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual String createStringFromAscii(const char* str, size_t length) override
    {
        return createString(_context.newValue(std::string_view { str, length }));
    }

    virtual String createStringFromUtf8(const uint8_t* utf8, size_t length) override
    {
        return createString(_context.newValue(std::string_view { (const char*) utf8, length }));
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
        // TODO: NYI
        std::abort();
    }

    virtual std::shared_ptr<HostObject> getHostObject(const Object&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual HostFunctionType& getHostFunction(const Function&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual Value getProperty(const Object& obj, const PropNameID& name) override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(obj));

        auto propName = utf8(name);

        return createValue(qjsObjectValue->_val[propName.c_str()]);
    }

    virtual Value getProperty(const Object& obj, const String& name) override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(obj));

        auto propName = utf8(name);

        return createValue(qjsObjectValue->_val[propName.c_str()]);
    }

    virtual bool hasProperty(const Object&, const PropNameID& name) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool hasProperty(const Object&, const String& name) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual void setPropertyValue(Object& obj, const PropNameID& name, const Value& value) override
    {
        QuickJSPointerValue* qjsObjectValue =
            static_cast<QuickJSPointerValue*>(const_cast<PointerValue*>(getPointerValue(obj)));

        auto propName = utf8(name);

        qjsObjectValue->_val[propName.c_str()] = fromJSIValue(value);
    }

    virtual void setPropertyValue(Object& obj, const String& name, const Value& value) override
    {
        QuickJSPointerValue* qjsObjectValue =
            static_cast<QuickJSPointerValue*>(const_cast<PointerValue*>(getPointerValue(obj)));

        auto propName = utf8(name);

        qjsObjectValue->_val[propName.c_str()] = fromJSIValue(value);
    }

    virtual bool isArray(const Object& obj) const override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(obj));

        return qjsObjectValue->_val.isArray();
    }

    virtual bool isArrayBuffer(const Object&) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool isFunction(const Object& obj) const override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(obj));

        return qjsObjectValue->_val.isFunction();
    }

    virtual bool isHostObject(const Object&) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool isHostFunction(const Function&) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual Array getPropertyNames(const Object&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual WeakObject createWeakObject(const Object&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual Value lockWeakObject(const WeakObject&) override
    {
        // TODO: NYI
        std::abort();
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
        // TODO: NYI
        std::abort();
    }

    virtual uint8_t* data(const ArrayBuffer&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual Value getValueAtIndex(const Array&, size_t i) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual void setValueAtIndexImpl(Array&, size_t i, const Value& value) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual Function createFunctionFromHostFunction(const PropNameID& name, unsigned int paramCount, HostFunctionType func) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual Value call(const Function&, const Value& jsThis, const Value* args, size_t count) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual Value callAsConstructor(const Function&, const Value* args, size_t count) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool strictEquals(const Symbol& a, const Symbol& b) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool strictEquals(const String& a, const String& b) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool strictEquals(const Object& a, const Object& b) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool instanceOf(const Object& o, const Function& f) override
    {
        // TODO: NYI
        std::abort();
    }
};

std::unique_ptr<Runtime> __cdecl makeQuickJSRuntime(QuickJSRuntimeArgs&& args)
{
    return std::make_unique<QuickJSRuntime>(std::move(args));
}

}
