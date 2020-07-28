#include <iostream>
#include <array>
#include <memory>
#include <mutex>
#include <sstream>

#include <quickjs-libc.h>
#include <quickjspp.hpp>

#include "QuickJSRuntime.h"

using namespace facebook;

#define QJS_VERIFY_ELSE_CRASH(condition) \
  do {                                   \
    if (!(condition)) {                  \
      assert(false && #condition);       \
      std::terminate();                  \
    }                                    \
  } while (false)

#define QJS_VERIFY_ELSE_CRASH_MSG(condition, message) \
  do {                                                \
    if (!(condition)) {                               \
      assert(false && (message));                     \
      std::terminate();                               \
    }                                                 \
  } while (false)

namespace quickjs {

namespace {
std::once_flag g_hostObjectClassOnceFlag;
JSClassID g_hostObjectClassId;
JSClassExoticMethods g_hostObjectExoticMethods;
JSClassDef g_hostObjectClassDef;
} // namespace

static constexpr size_t MaxCallArgCount = 32;

class QuickJSRuntime : public jsi::Runtime
{
private:
    qjs::Runtime _runtime;
    qjs::Context _context;

    class QuickJSPointerValue final : public jsi::Runtime::PointerValue
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

        static JSValue GetJSValue(const PointerValue* pv) noexcept
        {
            return static_cast<const QuickJSPointerValue*>(pv)->_val.v;
        }

    private:
        qjs::Value _val;

    protected:
        friend class QuickJSRuntime;
    };

    // Property ID in QuickJS are Atoms
    struct QuickJSAtomPointerValue final : public jsi::Runtime::PointerValue
    {
        QuickJSAtomPointerValue(JSContext* context, JSAtom atom)
            : _context { context }
            , _atom { atom }
        {
        }

        QuickJSAtomPointerValue(const QuickJSAtomPointerValue& other)
            : _context { other._context }
            , _atom { other._atom }
        {
            if (_context)
            {
                _atom = JS_DupAtom(_context, _atom);
            }
        }

        void invalidate() override
        {
            if (_context)
            {
                JS_FreeAtom(_context, _atom);
            }

            delete this;
        }

        static JSAtom GetJSAtom(const PointerValue* pv) noexcept
        {
            return static_cast<const QuickJSAtomPointerValue*>(pv)->_atom;
        }

    private:
        JSContext* _context;
        JSAtom _atom;
    };

    jsi::String createString(qjs::Value&& val)
    {
        return make<jsi::String>(new QuickJSPointerValue(std::move(val)));
    }

    jsi::PropNameID createPropNameID(JSAtom atom)
    {
        return make<jsi::PropNameID>(new QuickJSAtomPointerValue { _context.ctx, atom });
    }

    jsi::Object createObject(qjs::Value&& val)
    {
        return make<jsi::Object>(new QuickJSPointerValue(std::move(val)));
    }

    jsi::String throwException(qjs::Value&& val)
    {
        // TODO:
        return make<jsi::String>(new QuickJSPointerValue(_context.getException()));
    }

    qjs::Value fromJSIValue(const jsi::Value& value)
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

    jsi::Value createValue(JSValue&& jsValue)
    {
        return createValue(_context.newValue(std::move(jsValue)));
    }

    jsi::Value createValue(qjs::Value&& val)
    {
        switch (val.getTag())
        {
            case JS_TAG_INT:
                return jsi::Value(static_cast<int>(val.as<int64_t>()));

            case JS_TAG_FLOAT64:
                return jsi::Value(val.as<double>());

            case JS_TAG_BOOL:
                return jsi::Value(val.as<bool>());

            case JS_TAG_UNDEFINED:
                return jsi::Value();

            case JS_TAG_NULL:
            case JS_TAG_UNINITIALIZED:
                return jsi::Value(nullptr);

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
                return jsi::Value();
        }
    }

    static JSAtom AsJSAtom(const jsi::PropNameID& propertyId) noexcept
    {
        return QuickJSAtomPointerValue::GetJSAtom(getPointerValue(propertyId));
    }

    template <typename T>
    static JSValue AsJSValue(const T& obj) noexcept
    {
        return QuickJSPointerValue::GetJSValue(getPointerValue(obj));
    }

    JSValue AsJSValue(const jsi::Value& value) noexcept
    {
        return fromJSIValue(value).v;
    }

    JSValueConst AsJSValueConst(const jsi::Value& value) noexcept
    {
        if (value.isUndefined())
        {
            return JS_UNDEFINED;
        }
        else if (value.isNull())
        {
            return JS_NULL;
        }
        else if (value.isBool())
        {
            return value.getBool() ? JS_TRUE : JS_FALSE;
        }
        else if (value.isNumber())
        {
            return JS_NewFloat64(_context.ctx, value.getNumber());
        }
        else if (value.isString())
        {
            return QuickJSPointerValue::GetJSValue(getPointerValue(value));
        }
        else if (value.isString())
        {
            return QuickJSPointerValue::GetJSValue(getPointerValue(value));
        }
        else if (value.isObject())
        {
            return QuickJSPointerValue::GetJSValue(getPointerValue(value));
        }
        else
        {
            QJS_VERIFY_ELSE_CRASH_MSG(false, "Unknown JSI Value kind");
        }
    }

    JSValueConst AsJSValueConst(const jsi::Pointer& ptr) noexcept
    {
        return QuickJSPointerValue::GetJSValue(getPointerValue(ptr));
    }

    std::string getExceptionDetails()
    {
        auto exc = _context.getException();

        std::stringstream strstream;
        strstream << (std::string) exc << std::endl;

        if (JS_HasProperty(_context.ctx, exc.v, JS_NewAtom(_context.ctx, "stack")))
        {
            strstream << (std::string) exc["stack"] << std::endl;
        }

        return strstream.str();
    }

public:
    QuickJSRuntime(QuickJSRuntimeArgs&& args) :
        _runtime(), _context(_runtime)
    {
    }

    ~QuickJSRuntime()
    {
    }

    virtual jsi::Value evaluateJavaScript(const std::shared_ptr<const jsi::Buffer>& buffer, const std::string& sourceURL) override
    {
        try
        {
            auto val = _context.eval(reinterpret_cast<const char*>(buffer->data()), sourceURL.c_str(), JS_EVAL_TYPE_GLOBAL);
            auto result = createValue(std::move(val));
            return result;
        }
        catch (qjs::exception&)
        {
            throw jsi::JSError(*this, getExceptionDetails());
        }
    }

    virtual std::shared_ptr<const jsi::PreparedJavaScript> prepareJavaScript(const std::shared_ptr<const jsi::Buffer>& buffer, std::string sourceURL) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual jsi::Value evaluatePreparedJavaScript(const std::shared_ptr<const jsi::PreparedJavaScript>& js) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual jsi::Object global() override
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
        return new QuickJSAtomPointerValue { *static_cast<const QuickJSAtomPointerValue*>(pv) };
    }

    virtual jsi::PropNameID createPropNameIDFromAscii(const char* str, size_t length) override
    {
        return createPropNameID(JS_NewAtomLen(_context.ctx, str, length));
    }

    virtual jsi::PropNameID createPropNameIDFromUtf8(const uint8_t* utf8, size_t length) override
    {
        return createPropNameID(JS_NewAtomLen(_context.ctx, (const char*) utf8, length));
    }

    virtual jsi::PropNameID createPropNameIDFromString(const jsi::String& str) override
    {
        return createPropNameID(JS_ValueToAtom(_context.ctx, AsJSValueConst(str)));
    }

    virtual std::string utf8(const jsi::PropNameID& sym) override
    {
        const char* str = JS_AtomToCString(_context.ctx, AsJSAtom(sym));
        if (!str)
        {
            // TODO: NYI - report error
            std::abort();
        }
        std::string result { str };
        JS_FreeCString(_context.ctx, str);
        return result;
    }

    virtual bool compare(const jsi::PropNameID& left, const jsi::PropNameID& right) override
    {
        return AsJSAtom(left) == AsJSAtom(right);
    }

    virtual std::string symbolToString(const jsi::Symbol&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual jsi::String createStringFromAscii(const char* str, size_t length) override
    {
        return createString(_context.newValue(std::string_view { str, length }));
    }

    virtual jsi::String createStringFromUtf8(const uint8_t* utf8, size_t length) override
    {
        return createString(_context.newValue(std::string_view { (const char*) utf8, length }));
    }

    virtual std::string utf8(const jsi::String& str) override
    {
        const QuickJSPointerValue* qjsStringValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(str));

        return qjsStringValue->_val.as<std::string>();
    }

    virtual jsi::Object createObject() override
    {
        return createObject(_context.newObject());
    }

    struct HostObjectProxyBase
    {
        HostObjectProxyBase(QuickJSRuntime& runtime, std::shared_ptr<jsi::HostObject>&& hostObject) noexcept
            : m_runtime { runtime }
            , m_hostObject { std::move(hostObject) }
        {
        }

        QuickJSRuntime& m_runtime;
        std::shared_ptr<jsi::HostObject>&& m_hostObject;
    };

    virtual jsi::Object createObject(std::shared_ptr<jsi::HostObject> hostObject) override
    {
        struct HostObjectProxy : HostObjectProxyBase
        {
            HostObjectProxy(QuickJSRuntime& runtime, std::shared_ptr<jsi::HostObject>&& hostObject) noexcept
                : HostObjectProxyBase { runtime, std::move(hostObject) }
            {
            }

            static int GetOwnProperty(JSContext* ctx, JSPropertyDescriptor* desc, JSValueConst obj, JSAtom prop)
            {
                // TODO: NYI
                std::abort();
            }

            static int GetOwnPropertyNames(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj)
            {
                // TODO: NYI
                std::abort();
            }

            static int DeleteProperty(JSContext* ctx, JSValueConst obj, JSAtom prop)
            {
                // TODO: NYI
                std::abort();
            }

            static int DefineOwnProperty(JSContext* ctx, JSValueConst this_obj, JSAtom prop,
                JSValueConst val, JSValueConst getter, JSValueConst setter, int flags)
            {
                // TODO: NYI
                std::abort();
            }

            static void Finalize(JSRuntime* rt, JSValue val)
            {
                // Take ownership of proxy object to delete it
                std::unique_ptr<HostObjectProxy> proxy { GetProxy(val) };
            }

            static HostObjectProxy* GetProxy(JSValue val)
            {
                return static_cast<HostObjectProxy*>(JS_GetOpaque(val, g_hostObjectClassId));
            }
        };

        // Register custom ClassDef for HostObject only one.
        // We use it to associate the HostObject with JSValue with help of opaque value
        // and to implement the HostObject proxy.
        std::call_once(g_hostObjectClassOnceFlag, [this]()
        {
            g_hostObjectExoticMethods = {};
            g_hostObjectExoticMethods.get_own_property = HostObjectProxy::GetOwnProperty;
            g_hostObjectExoticMethods.get_own_property_names = HostObjectProxy::GetOwnPropertyNames;
            g_hostObjectExoticMethods.delete_property = HostObjectProxy::DeleteProperty;
            g_hostObjectExoticMethods.define_own_property = HostObjectProxy::DefineOwnProperty;

            g_hostObjectClassDef = {};
            g_hostObjectClassDef.class_name = "HostObject";
            g_hostObjectClassDef.finalizer = HostObjectProxy::Finalize;
            g_hostObjectClassDef.exotic = &g_hostObjectExoticMethods;

            g_hostObjectClassId = JS_NewClassID(nullptr);
            //TODO: check error
            JS_NewClass(_runtime.rt, g_hostObjectClassId, &g_hostObjectClassDef);
        });

        //TODO: check error
        JSValue obj = JS_NewObjectClass(_context.ctx, g_hostObjectClassId);
        JS_SetOpaque(obj, new HostObjectProxy { *this, std::move(hostObject) });
        return createObject(_context.newValue(std::move(obj)));
    }

    virtual std::shared_ptr<jsi::HostObject> getHostObject(const jsi::Object&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual jsi::HostFunctionType& getHostFunction(const jsi::Function&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual jsi::Value getProperty(const jsi::Object& obj, const jsi::PropNameID& name) override
    {
        return createValue(JS_GetProperty(_context.ctx, AsJSValue(obj), AsJSAtom(name)));
    }

    virtual jsi::Value getProperty(const jsi::Object& obj, const jsi::String& name) override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(obj));

        auto propName = utf8(name);

        return createValue(qjsObjectValue->_val[propName.c_str()]);
    }

    virtual bool hasProperty(const jsi::Object& obj, const jsi::PropNameID& name) override
    {
        //TODO: handle exception
        //TODO: Should we free the atom?
        return JS_HasProperty(_context.ctx, AsJSValueConst(obj), AsJSAtom(name));
    }

    virtual bool hasProperty(const jsi::Object& obj, const jsi::String& name) override
    {
        //TODO: free atom
        //TODO: handle exception
        return JS_HasProperty(_context.ctx, AsJSValueConst(obj), JS_ValueToAtom(_context.ctx, AsJSValueConst(name)));
    }

    virtual void setPropertyValue(jsi::Object& obj, const jsi::PropNameID& name, const jsi::Value& value) override
    {
        JS_SetProperty(_context.ctx, AsJSValue(obj), AsJSAtom(name), fromJSIValue(value).v);
    }

    virtual void setPropertyValue(jsi::Object& obj, const jsi::String& name, const jsi::Value& value) override
    {
        QuickJSPointerValue* qjsObjectValue =
            static_cast<QuickJSPointerValue*>(const_cast<PointerValue*>(getPointerValue(obj)));

        auto propName = utf8(name);

        qjsObjectValue->_val[propName.c_str()] = fromJSIValue(value);
    }

    virtual bool isArray(const jsi::Object& obj) const override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(obj));

        return qjsObjectValue->_val.isArray();
    }

    virtual bool isArrayBuffer(const jsi::Object&) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool isFunction(const jsi::Object& obj) const override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(obj));

        return qjsObjectValue->_val.isFunction();
    }

    virtual bool isHostObject(const jsi::Object&) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool isHostFunction(const jsi::Function&) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual jsi::Array getPropertyNames(const jsi::Object& obj) override
    {
        // Handle to the null JS value.
        qjs::Value jsNull = _context.newValue(JS_NULL);

        // Handle to the Object constructor.
        qjs::Value objectConstructor = _context.global()["Object"];

        // Handle to the Object.prototype Object.
        qjs::Value objectPrototype = objectConstructor["prototype"];

        // Handle to the Object.prototype.propertyIsEnumerable() Function.
        qjs::Value objectPrototypePropertyIsEnumerable = objectPrototype["propertyIsEnumerable"];

        // We now traverse the object's property chain and collect all enumerable
        // property names.
        std::vector < qjs::Value> enumerablePropNames {};
        qjs::Value currentObjectOnPrototypeChain = _context.newValue(AsJSValue(obj));

        // We have a small optimization here where we stop traversing the prototype
        // chain as soon as we hit Object.prototype. However, we still need to check
        // for null here, as one can create an Object with no prototype through
        // Object.create(null).
        while (currentObjectOnPrototypeChain != objectPrototype &&
            currentObjectOnPrototypeChain != jsNull)
        {
            JSPropertyEnum* propNamesEnum;
            uint32_t propNamesSize;
            //TODO: check error
            JS_GetOwnPropertyNames(_context.ctx, &propNamesEnum, &propNamesSize, currentObjectOnPrototypeChain.v, 0);

            for (uint32_t i = 0; i < propNamesSize; ++i)
            {
                JSPropertyEnum* propName = propNamesEnum + i;
                if (propName->is_enumerable)
                {
                    enumerablePropNames.emplace_back(_context.newValue(JS_AtomToValue(_context.ctx, propName->atom)));
                }
            }

            currentObjectOnPrototypeChain = _context.newValue(JS_GetPrototype(_context.ctx, currentObjectOnPrototypeChain.v));
        }

        size_t enumerablePropNamesSize = enumerablePropNames.size();
        facebook::jsi::Array result = createArray(enumerablePropNamesSize);

        for (size_t i = 0; i < enumerablePropNamesSize; ++i)
        {
            result.setValueAtIndex(*this, i, createString(std::move(enumerablePropNames[i])));
        }

        return result;
    }

    virtual jsi::WeakObject createWeakObject(const jsi::Object&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual jsi::Value lockWeakObject(const jsi::WeakObject&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual jsi::Array createArray(size_t length) override
    {
        return createObject(qjs::Value { _context.ctx, JS_NewArray(_context.ctx) }).getArray(*this);
    }

    virtual size_t size(const jsi::Array& arr) override
    {
        const QuickJSPointerValue* qjsObjectValue =
            static_cast<const QuickJSPointerValue*>(getPointerValue(arr));

        return static_cast<int32_t>(qjsObjectValue->_val["length"]);
    }

    virtual size_t size(const jsi::ArrayBuffer&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual uint8_t* data(const jsi::ArrayBuffer&) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual jsi::Value getValueAtIndex(const jsi::Array&, size_t i) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual void setValueAtIndexImpl(jsi::Array&, size_t i, const jsi::Value& value) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual jsi::Function createFunctionFromHostFunction(const jsi::PropNameID& name, unsigned int paramCount, jsi::HostFunctionType func) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual jsi::Value call(const jsi::Function& func, const jsi::Value& jsThis, const jsi::Value* args, size_t count) override
    {
        QJS_VERIFY_ELSE_CRASH_MSG(count <= MaxCallArgCount, "Argument count must not exceed the supported max arg count.");
        std::array<JSValue, MaxCallArgCount> jsArgs;
        for (size_t i = 0; i < count; ++i)
        {
            jsArgs[i] = AsJSValueConst(*(args + i));
        }

        return createValue(JS_Call(_context.ctx, AsJSValueConst(func), AsJSValueConst(jsThis), static_cast<int>(count), jsArgs.data()));
    }

    virtual jsi::Value callAsConstructor(const jsi::Function&, const jsi::Value* args, size_t count) override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool strictEquals(const jsi::Symbol& a, const jsi::Symbol& b) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool strictEquals(const jsi::String& a, const jsi::String& b) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool strictEquals(const jsi::Object& a, const jsi::Object& b) const override
    {
        // TODO: NYI
        std::abort();
    }

    virtual bool instanceOf(const jsi::Object& o, const jsi::Function& f) override
    {
        // TODO: NYI
        std::abort();
    }
};

std::unique_ptr<jsi::Runtime> __cdecl makeQuickJSRuntime(QuickJSRuntimeArgs&& args)
{
    return std::make_unique<QuickJSRuntime>(std::move(args));
}

}
