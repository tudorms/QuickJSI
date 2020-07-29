#include <iostream>
#include <array>
#include <memory>
#include <string>
#include <mutex>
#include <sstream>
#include <unordered_set>

#include <quickjspp.hpp>

#include "QuickJSRuntime.h"

using namespace facebook;
using namespace std::string_literals;

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
JSClassID g_hostObjectClassId {};
JSClassExoticMethods g_hostObjectExoticMethods;
JSClassDef g_hostObjectClassDef;

std::once_flag g_hostFunctionClassOnceFlag;
JSClassID g_hostFunctionClassId {};
JSClassDef g_hostFunctionClassDef;
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

    JSValueConst AsJSValueConst(const jsi::Value& value) const noexcept
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

    JSValueConst AsJSValueConst(const jsi::Pointer& ptr) const noexcept
    {
        return QuickJSPointerValue::GetJSValue(getPointerValue(ptr));
    }

    template <typename T>
    static qjs::Value AsValue(const T& obj) noexcept
    {
        return QuickJSPointerValue::GetValue(getPointerValue(obj));
    }

    JSValue CloneJSValue(const jsi::Value& value) noexcept
    {
        return JS_DupValue(_context.ctx, AsJSValueConst(value));
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
        auto exc = self->_context.getException();
        std::string message;
        std::string stack;
        if (JS_HasProperty(_context.ctx, exc.v, JS_NewAtom(_context.ctx, "message")))
        {
            message = (std::string)exc["message"];
        }
        if (JS_HasProperty(_context.ctx, exc.v, JS_NewAtom(_context.ctx, "stack")))
        {
            stack = (std::string)exc["stack"];
        }

        throw jsi::JSError(*self, std::move(message), std::move(stack));
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

    static QuickJSRuntime* FromContext(JSContext* ctx)
    {
        return static_cast<QuickJSRuntime*>(JS_GetContextOpaque(ctx));
    }

    static int SetException(JSContext* ctx, const char* message, const char* stack)
    {
        JSValue errorObj = JS_NewError(ctx);
        if (JS_IsException(errorObj))
        {
            errorObj = JS_NULL;
        }
        else
        {
            if (!message)
            {
                message = "Unknown error";
            }

            JS_DefinePropertyValue(ctx, errorObj, JS_NewAtom(ctx, "message"),
                JS_NewString(ctx, message),
                JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);

            if (stack)
            {
                JS_DefinePropertyValue(ctx, errorObj, JS_NewAtom(ctx, "stack"),
                    JS_NewString(ctx, stack),
                    JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
            }
        }

        JS_Throw(ctx, errorObj);
        return -1;
    }

public:
    QuickJSRuntime(QuickJSRuntimeArgs&& args) :
        _runtime(), _context(_runtime)
    {
        JS_SetContextOpaque(_context.ctx, this);
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
        //std::abort();
        return nullptr;
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Value evaluatePreparedJavaScript(const std::shared_ptr<const jsi::PreparedJavaScript>& js) override try
    {
        // TODO: NYI
        //std::abort();
        return jsi::Value();
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
        HostObjectProxyBase(std::shared_ptr<jsi::HostObject>&& hostObject) noexcept
            : _hostObject { std::move(hostObject) }
        {
        }

        std::shared_ptr<jsi::HostObject> _hostObject;
    };

    virtual jsi::Object createObject(std::shared_ptr<jsi::HostObject> hostObject) override try
    {
        struct HostObjectProxy : HostObjectProxyBase
        {
            HostObjectProxy(std::shared_ptr<jsi::HostObject>&& hostObject) noexcept
                : HostObjectProxyBase { std::move(hostObject) }
            {
            }

            static int GetOwnProperty(JSContext* ctx, JSPropertyDescriptor* desc, JSValueConst obj, JSAtom prop) noexcept try
            {
                QuickJSRuntime* runtime = QuickJSRuntime::FromContext(ctx);
                auto proxy = GetProxy(ctx, obj);
                jsi::Value result = proxy->_hostObject->get(*runtime, runtime->createPropNameID(JS_DupAtom(ctx, prop)));
                // TODO: implement move here for result
                desc->value = runtime->CloneJSValue(result);
                return 1;
            }
            catch (const jsi::JSError& jsError)
            {
                return QuickJSRuntime::SetException(ctx, jsError.getMessage().c_str(), jsError.getStack().c_str());
            }
            catch (const std::exception& ex)
            {
                return QuickJSRuntime::SetException(ctx, ex.what(), nullptr);
            }
            catch (...)
            {
                return QuickJSRuntime::SetException(ctx, "Unexpected error", nullptr);
            }

            static int GetOwnPropertyNames(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj) noexcept try
            {
                *ptab = nullptr;
                *plen = 0;
                QuickJSRuntime* runtime = QuickJSRuntime::FromContext(ctx);
                auto proxy = GetProxy(ctx, obj);
                std::vector<jsi::PropNameID> propNames = proxy->_hostObject->getPropertyNames(*runtime);
                if (!propNames.empty())
                {
                    std::unordered_set<JSAtom> uniqueAtoms;
                    uniqueAtoms.reserve(propNames.size());
                    for (size_t i = 0; i < propNames.size(); ++i)
                    {
                        uniqueAtoms.insert(JS_DupAtom(ctx, AsJSAtom(propNames[i])));
                    }

                    *ptab = (JSPropertyEnum*) js_malloc(ctx, uniqueAtoms.size() * sizeof(JSPropertyEnum));
                    *plen = uniqueAtoms.size();
                    size_t index = 0;
                    for (auto atom : uniqueAtoms)
                    {
                        (*ptab + index)->atom = atom;
                        (*ptab + index)->is_enumerable = 1;
                        ++index;
                    }
                }

                return 0; // Must return 0 on success
            }
            catch (const jsi::JSError& jsError)
            {
                return QuickJSRuntime::SetException(ctx, jsError.getMessage().c_str(), jsError.getStack().c_str());
            }
            catch (const std::exception& ex)
            {
                return QuickJSRuntime::SetException(ctx, ex.what(), nullptr);
            }
            catch (...)
            {
                return QuickJSRuntime::SetException(ctx, "Unexpected error", nullptr);
            }

            static int SetProperty(JSContext* ctx, JSValueConst obj, JSAtom prop,
                JSValueConst value, JSValueConst receiver, int flags) noexcept try
            {
                QuickJSRuntime* runtime = QuickJSRuntime::FromContext(ctx);
                auto proxy = GetProxy(ctx, obj);
                //TODO: use reference type to avoid extra copy
                proxy->_hostObject->set(*runtime, runtime->createPropNameID(JS_DupAtom(ctx, prop)), runtime->createValue(JS_DupValue(ctx, value)));
                return 1;
            }
            catch (const jsi::JSError& jsError)
            {
                return QuickJSRuntime::SetException(ctx, jsError.getMessage().c_str(), jsError.getStack().c_str());
            }
            catch (const std::exception& ex)
            {
                return QuickJSRuntime::SetException(ctx, ex.what(), nullptr);
            }
            catch (...)
            {
                return QuickJSRuntime::SetException(ctx, "Unexpected error", nullptr);
            }

            static void Finalize(JSRuntime* rt, JSValue val) noexcept
            {
                // Take ownership of proxy object to delete it
                std::unique_ptr<HostObjectProxy> proxy { GetProxy(val) };
            }

            static HostObjectProxy* GetProxy(JSValue obj)
            {
                return static_cast<HostObjectProxy*>(JS_GetOpaque(obj, g_hostObjectClassId));
            }

            static HostObjectProxy* GetProxy(JSContext* ctx, JSValue obj)
            {
                return static_cast<HostObjectProxy*>(JS_GetOpaque2(ctx, obj, g_hostObjectClassId));
            }
        };

        // Register custom ClassDef for HostObject only once.
        // We use it to associate the HostObject with JSValue with help of opaque value
        // and to implement the HostObject proxy.
        std::call_once(g_hostObjectClassOnceFlag, [this]()
        {
            g_hostObjectExoticMethods = {};
            g_hostObjectExoticMethods.get_own_property = HostObjectProxy::GetOwnProperty;
            g_hostObjectExoticMethods.get_own_property_names = HostObjectProxy::GetOwnPropertyNames;
            g_hostObjectExoticMethods.set_property = HostObjectProxy::SetProperty;

            g_hostObjectClassDef = {};
            g_hostObjectClassDef.class_name = "HostObject";
            g_hostObjectClassDef.finalizer = HostObjectProxy::Finalize;
            g_hostObjectClassDef.exotic = &g_hostObjectExoticMethods;

            g_hostObjectClassId = JS_NewClassID(&g_hostObjectClassId);
        });

        if (!JS_IsRegisteredClass(_runtime.rt, g_hostObjectClassId))
        {
            CheckBool(JS_NewClass(_runtime.rt, g_hostObjectClassId, &g_hostObjectClassDef));
        }

        JSValue obj = CheckJSValue(JS_NewObjectClass(_context.ctx, g_hostObjectClassId));
        JS_SetOpaque(obj, new HostObjectProxy { std::move(hostObject) });
        return createPointerValue<jsi::Object>(_context.newValue(std::move(obj)));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual std::shared_ptr<jsi::HostObject> getHostObject(const jsi::Object& obj) override try
    {
        return static_cast<HostObjectProxyBase*>(JS_GetOpaque2(_context.ctx, AsJSValueConst(obj), g_hostObjectClassId))->_hostObject;
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::HostFunctionType& getHostFunction(const jsi::Function& func) override try
    {
        return static_cast<HostFunctionProxyBase*>(JS_GetOpaque2(_context.ctx, AsJSValueConst(func), g_hostFunctionClassId))->_hostFunction;
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

    virtual bool isHostObject(const jsi::Object& obj) const override try
    {
        return JS_GetOpaque2(_context.ctx, AsJSValueConst(obj), g_hostObjectClassId) != nullptr;
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual bool isHostFunction(const jsi::Function& func) const override try
    {
        return JS_GetOpaque2(_context.ctx, AsJSValueConst(func), g_hostFunctionClassId) != nullptr;
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Array getPropertyNames(const jsi::Object& obj) override try
    {
        // Handle to the Object constructor.
        qjs::Value objectConstructor = _context.global()["Object"];

        // Handle to the Object.prototype Object.
        qjs::Value objectPrototype = objectConstructor["prototype"];

        // We now traverse the object's property chain and collect all enumerable
        // property names.
        std::vector <qjs::Value> enumerablePropNames {};
        auto currentObjectOnPrototypeChain = AsJSValueConst(obj);

        // We have a small optimization here where we stop traversing the prototype
        // chain as soon as we hit Object.prototype. However, we still need to check
        // for null here, as one can create an Object with no prototype through
        // Object.create(null).
        while (JS_VALUE_GET_PTR(currentObjectOnPrototypeChain) != JS_VALUE_GET_PTR(objectPrototype.v) &&
            !JS_IsNull(currentObjectOnPrototypeChain))
        {
            JSPropertyEnum* propNamesEnum;
            uint32_t propNamesSize;
            //TODO: check error
            JS_GetOwnPropertyNames(_context.ctx, &propNamesEnum, &propNamesSize, currentObjectOnPrototypeChain, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);

            for (uint32_t i = 0; i < propNamesSize; ++i)
            {
                JSPropertyEnum* propName = propNamesEnum + i;
                if (propName->is_enumerable)
                {
                    enumerablePropNames.emplace_back(_context.ctx, JS_AtomToValue(_context.ctx, propName->atom));
                }

                JS_FreeAtom(_context.ctx, propName->atom);
            }

            js_free(_context.ctx, propNamesEnum);

            currentObjectOnPrototypeChain = JS_GetPrototype(_context.ctx, currentObjectOnPrototypeChain);
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

    struct HostFunctionProxyBase
    {
        HostFunctionProxyBase(jsi::HostFunctionType&& hostFunction)
            : _hostFunction { std::move(hostFunction) }
        {
        }

        jsi::HostFunctionType _hostFunction;
    };

    virtual jsi::Function createFunctionFromHostFunction(const jsi::PropNameID& name, unsigned int paramCount, jsi::HostFunctionType func) override try
    {
        struct HostFunctionProxy : HostFunctionProxyBase
        {
            HostFunctionProxy(jsi::HostFunctionType&& hostFunction)
                : HostFunctionProxyBase { std::move(hostFunction) }
            {
            }

            static JSValue Call(JSContext* ctx, JSValueConst func_obj,
                JSValueConst this_val, int argc, JSValueConst* argv,
                int flags) noexcept try
            {
                QJS_VERIFY_ELSE_CRASH_MSG(argc <= MaxCallArgCount, "Argument count must not exceed MaxCallArgCount");

                QuickJSRuntime* runtime = QuickJSRuntime::FromContext(ctx);
                auto proxy = GetProxy(ctx, func_obj);

                //TODO: avoid extra memory allocation here when creating jsi::Value
                jsi::Value thisArg = runtime->createValue(JS_DupValue(ctx, this_val));
                std::array<jsi::Value, MaxCallArgCount> args;
                for (int i = 0; i < argc; ++i)
                {
                    args[i] = runtime->createValue(JS_DupValue(ctx, argv[i]));
                }

                jsi::Value result = proxy->_hostFunction(*runtime, thisArg, args.data(), argc);
                //TODO: Implement move semantic instead for the return value
                return runtime->CloneJSValue(result);
            }
            catch (const jsi::JSError& jsError)
            {
                QuickJSRuntime::SetException(ctx, jsError.getMessage().c_str(), jsError.getStack().c_str());
                return JS_EXCEPTION;
            }
            catch (const std::exception& ex)
            {
                QuickJSRuntime::SetException(ctx, ("Exception in HostFunction: "s + ex.what()).c_str(), nullptr);
                return JS_EXCEPTION;
            }
            catch (...)
            {
                QuickJSRuntime::SetException(ctx, "Exception in HostFunction: <unknown>", nullptr);
                return JS_EXCEPTION;
            }

            static void Finalize(JSRuntime* rt, JSValue val) noexcept
            {
                // Take ownership of proxy object to delete it
                std::unique_ptr<HostFunctionProxy> proxy { GetProxy(val) };
            }

            static HostFunctionProxy* GetProxy(JSValue obj)
            {
                return static_cast<HostFunctionProxy*>(JS_GetOpaque(obj, g_hostFunctionClassId));
            }

            static HostFunctionProxy* GetProxy(JSContext* ctx, JSValue obj)
            {
                return static_cast<HostFunctionProxy*>(JS_GetOpaque2(ctx, obj, g_hostFunctionClassId));
            }
        };

        // Register custom ClassDef for HostFunction only once.
        // We use it to associate the HostFunction with JSValue with help of opaque value
        // and to implement the HostFunction proxy.
        std::call_once(g_hostFunctionClassOnceFlag, [this]()
        {
            g_hostFunctionClassDef = {};
            g_hostFunctionClassDef.class_name = "HostFunction";
            g_hostFunctionClassDef.call = HostFunctionProxy::Call;
            g_hostFunctionClassDef.finalizer = HostFunctionProxy::Finalize;

            g_hostFunctionClassId = JS_NewClassID(&g_hostFunctionClassId);
        });

        if (!JS_IsRegisteredClass(_runtime.rt, g_hostFunctionClassId))
        {
            CheckBool(JS_NewClass(_runtime.rt, g_hostFunctionClassId, &g_hostFunctionClassDef));
        }

        qjs::Value funcCtor = _context.global()["Function"];
        qjs::Value funcObj = _context.newValue(CheckJSValue(JS_NewObjectProtoClass(
            _context.ctx, JS_GetPrototype(_context.ctx, funcCtor.v), g_hostFunctionClassId)));

        JS_SetOpaque(funcObj.v, new HostFunctionProxy { std::move(func) });

        JS_DefineProperty(_context.ctx, funcObj.v, JS_NewAtom(_context.ctx, "length"), JS_NewUint32(_context.ctx, paramCount),
            JS_UNDEFINED, JS_UNDEFINED, JS_PROP_HAS_VALUE | JS_PROP_HAS_CONFIGURABLE);

        JSAtom funcNameAtom = AsJSAtom(name);
        qjs::Value funcNameValue = _context.newValue(JS_AtomToValue(_context.ctx, funcNameAtom));
        JS_DefineProperty(_context.ctx, funcObj.v, JS_NewAtom(_context.ctx, "name"), funcNameValue.v,
            JS_UNDEFINED, JS_UNDEFINED, JS_PROP_HAS_VALUE);

        return createPointerValue<jsi::Object>(std::move(funcObj)).getFunction(*this);
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Value call(const jsi::Function& func, const jsi::Value& jsThis, const jsi::Value* args, size_t count) override try
    {
        QJS_VERIFY_ELSE_CRASH_MSG(count <= MaxCallArgCount, "Argument count must not exceed the supported max arg count.");
        std::array<JSValue, MaxCallArgCount> jsArgsConst;
        for (size_t i = 0; i < count; ++i)
        {
            jsArgsConst[i] = AsJSValueConst(*(args + i));
        }

        auto funcValConst = AsJSValueConst(func);
        auto thisValConst = AsJSValueConst(jsThis);

        return createValue(JS_Call(_context.ctx, funcValConst, thisValConst, static_cast<int>(count), jsArgsConst.data()));
    }
    catch (qjs::exception&)
    {
        ThrowJSError();
    }

    virtual jsi::Value callAsConstructor(const jsi::Function& func, const jsi::Value* args, size_t count) override try
    {
        QJS_VERIFY_ELSE_CRASH_MSG(count <= MaxCallArgCount, "Argument count must not exceed the supported max arg count.");
        std::array<JSValue, MaxCallArgCount> jsArgsConst;
        for (size_t i = 0; i < count; ++i)
        {
            jsArgsConst[i] = AsJSValueConst(*(args + i));
        }

        auto funcValConst = AsJSValueConst(func);

        return createValue(JS_CallConstructor(_context.ctx, funcValConst, static_cast<int>(count), jsArgsConst.data()));
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
