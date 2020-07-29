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

        static qjs::Value GetValue(const PointerValue* pv) noexcept
        {
            return static_cast<const QuickJSPointerValue*>(pv)->_val;
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

    template <typename T>
    T createPointerValue(qjs::Value&& val)
    {
        return make<T>(new QuickJSPointerValue(std::move(val)));
    }

    jsi::PropNameID createPropNameID(JSAtom atom)
    {
        return make<jsi::PropNameID>(new QuickJSAtomPointerValue { _context.ctx, atom });
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
        else if (value.isSymbol())
        {
            // TODO: Validate me!
            return AsValue(value.getSymbol(*this));
        }
        else if (value.isString())
        {
            // TODO: Validate me!
            return AsValue(value.getString(*this));
        }
        else if (value.isObject())
        {
            // TODO: Validate me!
            return AsValue(value.getObject(*this));
        }
        else
        {
            std::abort();
        }
    }

    jsi::Value createValue(JSValue&& jsValue)
    {
        if (JS_IsException(jsValue))
        {
            ThrowJSError();
        }

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
                return createPointerValue<jsi::String>(std::move(val));

            case JS_TAG_OBJECT:
                return createPointerValue<jsi::Object>(std::move(val));

            case JS_TAG_SYMBOL:
                return createPointerValue<jsi::Symbol>(std::move(val));

            case JS_TAG_EXCEPTION:
                return throwException(std::move(val));

                // TODO: rest of types
            case JS_TAG_BIG_DECIMAL:
            case JS_TAG_BIG_INT:
            case JS_TAG_BIG_FLOAT:
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
        else if (value.isSymbol())
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

    template <typename T>
    static qjs::Value AsValue(const T& obj) noexcept
    {
        return QuickJSPointerValue::GetValue(getPointerValue(obj));
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

    [[noreturn]]
    void ThrowJSError() const
    {
        auto self = const_cast<QuickJSRuntime*>(this);
        throw jsi::JSError(*self, self->getExceptionDetails());
    }

    // Throw if value is negative. It indicates an error. 
    int CheckBool(int value)
    {
        if (value < 0)
        {
            ThrowJSError();
        }

        return value;
    }

    JSValue CheckJSValue(JSValue&& value)
    {
        if (JS_IsException(value))
        {
            ThrowJSError();
        }

        return std::move(value);
    }

public:
    QuickJSRuntime(QuickJSRuntimeArgs&& args) :
        _runtime(), _context(_runtime)
    {
    }

    ~QuickJSRuntime()
    {
    }

    virtual jsi::Value evaluateJavaScript(const std::shared_ptr<const jsi::Buffer>& buffer, const std::string& sourceURL) override try
    {
        auto val = _context.eval(reinterpret_cast<const char*>(buffer->data()), sourceURL.c_str(), JS_EVAL_TYPE_GLOBAL);
        auto result = createValue(std::move(val));
        return result;
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual std::shared_ptr<const jsi::PreparedJavaScript> prepareJavaScript(const std::shared_ptr<const jsi::Buffer>& buffer, std::string sourceURL) override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Value evaluatePreparedJavaScript(const std::shared_ptr<const jsi::PreparedJavaScript>& js) override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Object global() override try
    {
        return createPointerValue<jsi::Object>(_context.global());
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual std::string description() override
    {
        return "QuickJS";
    }

    virtual bool isInspectable() override
    {
        return false;
    }

    virtual PointerValue* cloneSymbol(const Runtime::PointerValue* pv) override try
    {
        return new QuickJSPointerValue(QuickJSPointerValue::GetValue(pv));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual PointerValue* cloneString(const Runtime::PointerValue* pv) override try
    {
        return new QuickJSPointerValue(QuickJSPointerValue::GetValue(pv));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual PointerValue* cloneObject(const Runtime::PointerValue* pv) override try
    {
        return new QuickJSPointerValue(QuickJSPointerValue::GetValue(pv));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual PointerValue* clonePropNameID(const Runtime::PointerValue* pv) override try
    {
        return new QuickJSAtomPointerValue { *static_cast<const QuickJSAtomPointerValue*>(pv) };
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::PropNameID createPropNameIDFromAscii(const char* str, size_t length) override try
    {
        return createPropNameID(JS_NewAtomLen(_context.ctx, str, length));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::PropNameID createPropNameIDFromUtf8(const uint8_t* utf8, size_t length) override try
    {
        return createPropNameID(JS_NewAtomLen(_context.ctx, (const char*) utf8, length));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::PropNameID createPropNameIDFromString(const jsi::String& str) override try
    {
        return createPropNameID(JS_ValueToAtom(_context.ctx, AsJSValueConst(str)));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual std::string utf8(const jsi::PropNameID& sym) override try
    {
        const char* str = JS_AtomToCString(_context.ctx, AsJSAtom(sym));
        if (!str)
        {
            ThrowJSError();
        }
        std::string result { str };
        JS_FreeCString(_context.ctx, str);
        return result;
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool compare(const jsi::PropNameID& left, const jsi::PropNameID& right) override try
    {
        return AsJSAtom(left) == AsJSAtom(right);
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual std::string symbolToString(const jsi::Symbol& sym) override try
    {
        try
        {
            auto symVal = AsValue(sym);
            auto toString = symVal["toString"].as<qjs::Value>();

            auto result = qjs::Value { _context.ctx, JS_Call(_context.ctx, toString.v, symVal.v, 0, nullptr) };

            return result.as<std::string>();
        }
        catch (qjs::exception&)
        {
            throw jsi::JSError(*this, getExceptionDetails());
        }
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::String createStringFromAscii(const char* str, size_t length) override try
    {
        return createPointerValue<jsi::String>(_context.newValue(std::string_view { str, length }));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::String createStringFromUtf8(const uint8_t* utf8, size_t length) override try
    {
        return createPointerValue<jsi::String>(_context.newValue(std::string_view { (const char*) utf8, length }));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual std::string utf8(const jsi::String& str) override try
    {
        return AsValue(str).as<std::string>();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Object createObject() override try
    {
        return createPointerValue<jsi::Object>(_context.newObject());
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
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

    virtual jsi::Object createObject(std::shared_ptr<jsi::HostObject> hostObject) override try
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
            CheckBool(JS_NewClass(_runtime.rt, g_hostObjectClassId, &g_hostObjectClassDef));
        });

        JSValue obj = CheckJSValue(JS_NewObjectClass(_context.ctx, g_hostObjectClassId));
        JS_SetOpaque(obj, new HostObjectProxy { *this, std::move(hostObject) });
        return createPointerValue<jsi::Object>(_context.newValue(std::move(obj)));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual std::shared_ptr<jsi::HostObject> getHostObject(const jsi::Object&) override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::HostFunctionType& getHostFunction(const jsi::Function&) override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Value getProperty(const jsi::Object& obj, const jsi::PropNameID& name) override try
    {
        return createValue(JS_GetProperty(_context.ctx, AsJSValue(obj), AsJSAtom(name)));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Value getProperty(const jsi::Object& obj, const jsi::String& name) override try
    {
        auto propName = utf8(name);

        return createValue(AsValue(obj)[propName.c_str()]);
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool hasProperty(const jsi::Object& obj, const jsi::PropNameID& name) override try
    {
        return CheckBool(JS_HasProperty(_context.ctx, AsJSValueConst(obj), AsJSAtom(name)));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool hasProperty(const jsi::Object& obj, const jsi::String& name) override try
    {
        return CheckBool(JS_HasProperty(_context.ctx, AsJSValueConst(obj), JS_ValueToAtom(_context.ctx, AsJSValueConst(name))));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual void setPropertyValue(jsi::Object& obj, const jsi::PropNameID& name, const jsi::Value& value) override try
    {
        auto propName = utf8(name);

        AsValue(obj)[propName.c_str()] = fromJSIValue(value);
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual void setPropertyValue(jsi::Object& obj, const jsi::String& name, const jsi::Value& value) override try
    {
        auto propName = utf8(name);

        AsValue(obj)[propName.c_str()] = fromJSIValue(value);
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool isArray(const jsi::Object& obj) const override try
    {
        return AsValue(obj).isArray();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool isArrayBuffer(const jsi::Object&) const override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool isFunction(const jsi::Object& obj) const override try
    {
        return AsValue(obj).isFunction();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool isHostObject(const jsi::Object&) const override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool isHostFunction(const jsi::Function&) const override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Array getPropertyNames(const jsi::Object& obj) override try
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
            JS_GetOwnPropertyNames(_context.ctx, &propNamesEnum, &propNamesSize, currentObjectOnPrototypeChain.v, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);

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
            result.setValueAtIndex(*this, i, createPointerValue<jsi::String>(std::move(enumerablePropNames[i])));
        }

        return result;
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::WeakObject createWeakObject(const jsi::Object&) override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Value lockWeakObject(const jsi::WeakObject&) override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Array createArray(size_t length) override try
    {
        // Note that in ECMAScript Array doesn't take length as a constructor argument (although many other engines do)
        auto arr = _context.newValue(JS_NewArray(_context.ctx));
        arr["length"] = static_cast<int64_t>(length);

        return createPointerValue<jsi::Object>(std::move(arr)).getArray(*this);
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual size_t size(const jsi::Array& arr) override try
    {
        return static_cast<int32_t>(AsValue(arr)["length"]);
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual size_t size(const jsi::ArrayBuffer&) override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual uint8_t* data(const jsi::ArrayBuffer&) override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Value getValueAtIndex(const jsi::Array& arr, size_t i) override try
    {
        return createValue(JS_GetPropertyUint32(_context.ctx, AsJSValueConst(arr), static_cast<uint32_t>(i)));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual void setValueAtIndexImpl(jsi::Array& arr, size_t i, const jsi::Value& value) override try
    {
        auto jsValue = AsJSValueConst(value);
        JS_DupValue(_context.ctx, jsValue);
        CheckBool(JS_SetPropertyUint32(_context.ctx, AsJSValueConst(arr), static_cast<uint32_t>(i), jsValue));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Function createFunctionFromHostFunction(const jsi::PropNameID& name, unsigned int paramCount, jsi::HostFunctionType func) override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Value call(const jsi::Function& func, const jsi::Value& jsThis, const jsi::Value* args, size_t count) override try
    {
        QJS_VERIFY_ELSE_CRASH_MSG(count <= MaxCallArgCount, "Argument count must not exceed the supported max arg count.");
        std::array<JSValue, MaxCallArgCount> jsArgs;
        // If we don't want to manually call dupe and free here, we could wrap them in qjs::Values instead
        for (size_t i = 0; i < count; ++i)
        {
            jsArgs[i] = JS_DupValue(_context.ctx, AsJSValueConst(*(args + i)));
        }

        auto funcVal = AsValue(func);
        auto thisVal = fromJSIValue(jsThis);

        auto result = qjs::Value { _context.ctx, JS_Call(_context.ctx, funcVal.v, thisVal.v, static_cast<int>(count), jsArgs.data()) };

        for (size_t i = 0; i < count; ++i)
        {
            JS_FreeValue(_context.ctx, jsArgs[i]);
        }

        return createValue(std::move(result));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Value callAsConstructor(const jsi::Function&, const jsi::Value* args, size_t count) override try
    {
        // TODO: NYI
        std::abort();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool strictEquals(const jsi::Symbol& a, const jsi::Symbol& b) const override try
    {
        return AsValue(a) == AsValue(b);
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool strictEquals(const jsi::String& a, const jsi::String& b) const override try
    {
        return AsValue(a).as<std::string>() == AsValue(b).as<std::string>();
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool strictEquals(const jsi::Object& a, const jsi::Object& b) const override try
    {
        return AsValue(a) == AsValue(b);
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool instanceOf(const jsi::Object& o, const jsi::Function& f) override try
    {
        return CheckBool(JS_IsInstanceOf(_context.ctx, AsJSValueConst(o), AsJSValueConst(f)));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }
};

std::unique_ptr<jsi::Runtime> __cdecl makeQuickJSRuntime(QuickJSRuntimeArgs&& args)
{
    return std::make_unique<QuickJSRuntime>(std::move(args));
}

}
