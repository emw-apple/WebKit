/*
    This file is part of the WebKit open source project.
    This file has been generated by generate-bindings.pl. DO NOT MODIFY!

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "JSTestIndexedSetterThrowingException.h"

#include "ActiveDOMObject.h"
#include "ContextDestructionObserverInlines.h"
#include "Document.h"
#include "DocumentInlines.h"
#include "ExtendedDOMClientIsoSubspaces.h"
#include "ExtendedDOMIsoSubspaces.h"
#include "JSDOMBinding.h"
#include "JSDOMConstructorNotConstructable.h"
#include "JSDOMConvertStrings.h"
#include "JSDOMExceptionHandling.h"
#include "JSDOMGlobalObjectInlines.h"
#include "JSDOMWrapperCache.h"
#include "Quirks.h"
#include "ScriptExecutionContext.h"
#include "WebCoreJSClientData.h"
#include <JavaScriptCore/FunctionPrototype.h>
#include <JavaScriptCore/HeapAnalyzer.h>
#include <JavaScriptCore/JSCInlines.h>
#include <JavaScriptCore/JSDestructibleObjectHeapCellType.h>
#include <JavaScriptCore/PropertyNameArray.h>
#include <JavaScriptCore/SlotVisitorMacros.h>
#include <JavaScriptCore/SubspaceInlines.h>
#include <wtf/GetPtr.h>
#include <wtf/PointerPreparations.h>
#include <wtf/URL.h>
#include <wtf/text/MakeString.h>

namespace WebCore {
using namespace JSC;

// Attributes

static JSC_DECLARE_CUSTOM_GETTER(jsTestIndexedSetterThrowingExceptionConstructor);

class JSTestIndexedSetterThrowingExceptionPrototype final : public JSC::JSNonFinalObject {
public:
    using Base = JSC::JSNonFinalObject;
    static JSTestIndexedSetterThrowingExceptionPrototype* create(JSC::VM& vm, JSDOMGlobalObject* globalObject, JSC::Structure* structure)
    {
        JSTestIndexedSetterThrowingExceptionPrototype* ptr = new (NotNull, JSC::allocateCell<JSTestIndexedSetterThrowingExceptionPrototype>(vm)) JSTestIndexedSetterThrowingExceptionPrototype(vm, globalObject, structure);
        ptr->finishCreation(vm);
        return ptr;
    }

    DECLARE_INFO;
    template<typename CellType, JSC::SubspaceAccess>
    static JSC::GCClient::IsoSubspace* subspaceFor(JSC::VM& vm)
    {
        STATIC_ASSERT_ISO_SUBSPACE_SHARABLE(JSTestIndexedSetterThrowingExceptionPrototype, Base);
        return &vm.plainObjectSpace();
    }
    static JSC::Structure* createStructure(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::JSValue prototype)
    {
        return JSC::Structure::create(vm, globalObject, prototype, JSC::TypeInfo(JSC::ObjectType, StructureFlags), info());
    }

private:
    JSTestIndexedSetterThrowingExceptionPrototype(JSC::VM& vm, JSC::JSGlobalObject*, JSC::Structure* structure)
        : JSC::JSNonFinalObject(vm, structure)
    {
    }

    void finishCreation(JSC::VM&);
};
STATIC_ASSERT_ISO_SUBSPACE_SHARABLE(JSTestIndexedSetterThrowingExceptionPrototype, JSTestIndexedSetterThrowingExceptionPrototype::Base);

using JSTestIndexedSetterThrowingExceptionDOMConstructor = JSDOMConstructorNotConstructable<JSTestIndexedSetterThrowingException>;

template<> const ClassInfo JSTestIndexedSetterThrowingExceptionDOMConstructor::s_info = { "TestIndexedSetterThrowingException"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestIndexedSetterThrowingExceptionDOMConstructor) };

template<> JSValue JSTestIndexedSetterThrowingExceptionDOMConstructor::prototypeForStructure(JSC::VM& vm, const JSDOMGlobalObject& globalObject)
{
    UNUSED_PARAM(vm);
    return globalObject.functionPrototype();
}

template<> void JSTestIndexedSetterThrowingExceptionDOMConstructor::initializeProperties(VM& vm, JSDOMGlobalObject& globalObject)
{
    putDirect(vm, vm.propertyNames->length, jsNumber(0), JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum);
    JSString* nameString = jsNontrivialString(vm, "TestIndexedSetterThrowingException"_s);
    m_originalName.set(vm, this, nameString);
    putDirect(vm, vm.propertyNames->name, nameString, JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum);
    putDirect(vm, vm.propertyNames->prototype, JSTestIndexedSetterThrowingException::prototype(vm, globalObject), JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum | JSC::PropertyAttribute::DontDelete);
}

/* Hash table for prototype */

static const std::array<HashTableValue, 1> JSTestIndexedSetterThrowingExceptionPrototypeTableValues {
    HashTableValue { "constructor"_s, static_cast<unsigned>(PropertyAttribute::DontEnum), NoIntrinsic, { HashTableValue::GetterSetterType, jsTestIndexedSetterThrowingExceptionConstructor, 0 } },
};

const ClassInfo JSTestIndexedSetterThrowingExceptionPrototype::s_info = { "TestIndexedSetterThrowingException"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestIndexedSetterThrowingExceptionPrototype) };

void JSTestIndexedSetterThrowingExceptionPrototype::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    reifyStaticProperties(vm, JSTestIndexedSetterThrowingException::info(), JSTestIndexedSetterThrowingExceptionPrototypeTableValues, *this);
    JSC_TO_STRING_TAG_WITHOUT_TRANSITION();
}

const ClassInfo JSTestIndexedSetterThrowingException::s_info = { "TestIndexedSetterThrowingException"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestIndexedSetterThrowingException) };

JSTestIndexedSetterThrowingException::JSTestIndexedSetterThrowingException(Structure* structure, JSDOMGlobalObject& globalObject, Ref<TestIndexedSetterThrowingException>&& impl)
    : JSDOMWrapper<TestIndexedSetterThrowingException>(structure, globalObject, WTFMove(impl))
{
}

static_assert(!std::is_base_of<ActiveDOMObject, TestIndexedSetterThrowingException>::value, "Interface is not marked as [ActiveDOMObject] even though implementation class subclasses ActiveDOMObject.");

JSObject* JSTestIndexedSetterThrowingException::createPrototype(VM& vm, JSDOMGlobalObject& globalObject)
{
    auto* structure = JSTestIndexedSetterThrowingExceptionPrototype::createStructure(vm, &globalObject, globalObject.objectPrototype());
    structure->setMayBePrototype(true);
    return JSTestIndexedSetterThrowingExceptionPrototype::create(vm, &globalObject, structure);
}

JSObject* JSTestIndexedSetterThrowingException::prototype(VM& vm, JSDOMGlobalObject& globalObject)
{
    return getDOMPrototype<JSTestIndexedSetterThrowingException>(vm, globalObject);
}

JSValue JSTestIndexedSetterThrowingException::getConstructor(VM& vm, const JSGlobalObject* globalObject)
{
    return getDOMConstructor<JSTestIndexedSetterThrowingExceptionDOMConstructor, DOMConstructorID::TestIndexedSetterThrowingException>(vm, *jsCast<const JSDOMGlobalObject*>(globalObject));
}

void JSTestIndexedSetterThrowingException::destroy(JSC::JSCell* cell)
{
    JSTestIndexedSetterThrowingException* thisObject = static_cast<JSTestIndexedSetterThrowingException*>(cell);
    thisObject->JSTestIndexedSetterThrowingException::~JSTestIndexedSetterThrowingException();
}

bool JSTestIndexedSetterThrowingException::legacyPlatformObjectGetOwnProperty(JSObject* object, JSGlobalObject* lexicalGlobalObject, PropertyName propertyName, PropertySlot& slot, bool ignoreNamedProperties)
{
    UNUSED_PARAM(ignoreNamedProperties);
    auto throwScope = DECLARE_THROW_SCOPE(JSC::getVM(lexicalGlobalObject));
    auto* thisObject = jsCast<JSTestIndexedSetterThrowingException*>(object);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    if (auto index = parseIndex(propertyName)) {
        if (auto item = thisObject->wrapped().item(index.value()); !!item) [[likely]] {
            auto value = toJS<IDLDOMString>(*lexicalGlobalObject, throwScope, WTFMove(item));
            RETURN_IF_EXCEPTION(throwScope, false);
            slot.setValue(thisObject, 0, value);
            return true;
        }
    }
    return JSObject::getOwnPropertySlot(object, lexicalGlobalObject, propertyName, slot);
}

bool JSTestIndexedSetterThrowingException::getOwnPropertySlot(JSObject* object, JSGlobalObject* lexicalGlobalObject, PropertyName propertyName, PropertySlot& slot)
{
    bool ignoreNamedProperties = false;
    return legacyPlatformObjectGetOwnProperty(object, lexicalGlobalObject, propertyName, slot, ignoreNamedProperties);
}

bool JSTestIndexedSetterThrowingException::getOwnPropertySlotByIndex(JSObject* object, JSGlobalObject* lexicalGlobalObject, unsigned index, PropertySlot& slot)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    auto* thisObject = jsCast<JSTestIndexedSetterThrowingException*>(object);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    if (index <= MAX_ARRAY_INDEX) [[likely]] {
        if (auto item = thisObject->wrapped().item(index); !!item) [[likely]] {
            auto value = toJS<IDLDOMString>(*lexicalGlobalObject, throwScope, WTFMove(item));
            RETURN_IF_EXCEPTION(throwScope, false);
            slot.setValue(thisObject, 0, value);
            return true;
        }
    }
    return JSObject::getOwnPropertySlotByIndex(object, lexicalGlobalObject, index, slot);
}

void JSTestIndexedSetterThrowingException::getOwnPropertyNames(JSObject* object, JSGlobalObject* lexicalGlobalObject, PropertyNameArray& propertyNames, DontEnumPropertiesMode mode)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto* thisObject = jsCast<JSTestIndexedSetterThrowingException*>(object);
    ASSERT_GC_OBJECT_INHERITS(object, info());
    for (unsigned i = 0, count = thisObject->wrapped().length(); i < count; ++i)
        propertyNames.add(Identifier::from(vm, i));
    JSObject::getOwnPropertyNames(object, lexicalGlobalObject, propertyNames, mode);
}

bool JSTestIndexedSetterThrowingException::put(JSCell* cell, JSGlobalObject* lexicalGlobalObject, PropertyName propertyName, JSValue value, PutPropertySlot& putPropertySlot)
{
    auto* thisObject = jsCast<JSTestIndexedSetterThrowingException*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());

    if (thisObject != putPropertySlot.thisValue()) [[unlikely]]
        return JSObject::put(thisObject, lexicalGlobalObject, propertyName, value, putPropertySlot);
    auto throwScope = DECLARE_THROW_SCOPE(lexicalGlobalObject->vm());

    if (auto index = parseIndex(propertyName)) {
        auto nativeValue = convert<IDLDOMString>(*lexicalGlobalObject, value);
        if (nativeValue.hasException(throwScope)) [[unlikely]]
            return true;
        invokeFunctorPropagatingExceptionIfNecessary(*lexicalGlobalObject, throwScope, [&] { return thisObject->wrapped().setItem(index.value(), nativeValue.releaseReturnValue()); });
        return true;
    }

    throwScope.assertNoException();
    RELEASE_AND_RETURN(throwScope, JSObject::put(thisObject, lexicalGlobalObject, propertyName, value, putPropertySlot));
}

bool JSTestIndexedSetterThrowingException::putByIndex(JSCell* cell, JSGlobalObject* lexicalGlobalObject, unsigned index, JSValue value, bool shouldThrow)
{
    auto* thisObject = jsCast<JSTestIndexedSetterThrowingException*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());

    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);

    if (index <= MAX_ARRAY_INDEX) [[likely]] {
        auto nativeValue = convert<IDLDOMString>(*lexicalGlobalObject, value);
        if (nativeValue.hasException(throwScope)) [[unlikely]]
            return true;
        invokeFunctorPropagatingExceptionIfNecessary(*lexicalGlobalObject, throwScope, [&] { return thisObject->wrapped().setItem(index, nativeValue.releaseReturnValue()); });
        return true;
    }

    throwScope.assertNoException();
    auto propertyName = Identifier::from(vm, index);
    PutPropertySlot putPropertySlot(thisObject, shouldThrow);
    RELEASE_AND_RETURN(throwScope, ordinarySetSlow(lexicalGlobalObject, thisObject, propertyName, value, putPropertySlot.thisValue(), shouldThrow));
}

bool JSTestIndexedSetterThrowingException::defineOwnProperty(JSObject* object, JSGlobalObject* lexicalGlobalObject, PropertyName propertyName, const PropertyDescriptor& propertyDescriptor, bool shouldThrow)
{
    auto* thisObject = jsCast<JSTestIndexedSetterThrowingException*>(object);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());

    auto throwScope = DECLARE_THROW_SCOPE(lexicalGlobalObject->vm());

    if (auto index = parseIndex(propertyName)) {
        if (!propertyDescriptor.isDataDescriptor())
            return typeError(lexicalGlobalObject, throwScope, shouldThrow, "Cannot set indexed properties on this object"_s);
        auto nativeValue = convert<IDLDOMString>(*lexicalGlobalObject, propertyDescriptor.value());
        if (nativeValue.hasException(throwScope)) [[unlikely]]
            return true;
        invokeFunctorPropagatingExceptionIfNecessary(*lexicalGlobalObject, throwScope, [&] { return thisObject->wrapped().setItem(index.value(), nativeValue.releaseReturnValue()); });
        return true;
    }

    PropertyDescriptor newPropertyDescriptor = propertyDescriptor;
    throwScope.release();
    return JSObject::defineOwnProperty(object, lexicalGlobalObject, propertyName, newPropertyDescriptor, shouldThrow);
}

bool JSTestIndexedSetterThrowingException::deleteProperty(JSCell* cell, JSGlobalObject* lexicalGlobalObject, PropertyName propertyName, DeletePropertySlot& slot)
{
    auto& thisObject = *jsCast<JSTestIndexedSetterThrowingException*>(cell);
    SUPPRESS_UNCOUNTED_LOCAL auto& impl = thisObject.wrapped();

    // Temporary quirk for ungap/@custom-elements polyfill (rdar://problem/111008826), consider removing in 2025.
    if (auto* document = dynamicDowncast<Document>(jsDynamicCast<JSDOMGlobalObject*>(lexicalGlobalObject)->scriptExecutionContext())) {
        if (document->quirks().needsConfigurableIndexedPropertiesQuirk()) [[unlikely]]
            return JSObject::deleteProperty(cell, lexicalGlobalObject, propertyName, slot);
    }

    if (auto index = parseIndex(propertyName))
        return !impl.isSupportedPropertyIndex(index.value());
    return JSObject::deleteProperty(cell, lexicalGlobalObject, propertyName, slot);
}

bool JSTestIndexedSetterThrowingException::deletePropertyByIndex(JSCell* cell, JSGlobalObject* lexicalGlobalObject, unsigned index)
{
    UNUSED_PARAM(lexicalGlobalObject);
    auto& thisObject = *jsCast<JSTestIndexedSetterThrowingException*>(cell);
    SUPPRESS_UNCOUNTED_LOCAL auto& impl = thisObject.wrapped();

    // Temporary quirk for ungap/@custom-elements polyfill (rdar://problem/111008826), consider removing in 2025.
    if (auto* document = dynamicDowncast<Document>(jsDynamicCast<JSDOMGlobalObject*>(lexicalGlobalObject)->scriptExecutionContext())) {
        if (document->quirks().needsConfigurableIndexedPropertiesQuirk()) [[unlikely]]
            return JSObject::deletePropertyByIndex(cell, lexicalGlobalObject, index);
    }

    return !impl.isSupportedPropertyIndex(index);
}

JSC_DEFINE_CUSTOM_GETTER(jsTestIndexedSetterThrowingExceptionConstructor, (JSGlobalObject* lexicalGlobalObject, EncodedJSValue thisValue, PropertyName))
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    auto* prototype = jsDynamicCast<JSTestIndexedSetterThrowingExceptionPrototype*>(JSValue::decode(thisValue));
    if (!prototype) [[unlikely]]
        return throwVMTypeError(lexicalGlobalObject, throwScope);
    return JSValue::encode(JSTestIndexedSetterThrowingException::getConstructor(vm, prototype->globalObject()));
}

JSC::GCClient::IsoSubspace* JSTestIndexedSetterThrowingException::subspaceForImpl(JSC::VM& vm)
{
    return WebCore::subspaceForImpl<JSTestIndexedSetterThrowingException, UseCustomHeapCellType::No>(vm, "JSTestIndexedSetterThrowingException"_s,
        [] (auto& spaces) { return spaces.m_clientSubspaceForTestIndexedSetterThrowingException.get(); },
        [] (auto& spaces, auto&& space) { spaces.m_clientSubspaceForTestIndexedSetterThrowingException = std::forward<decltype(space)>(space); },
        [] (auto& spaces) { return spaces.m_subspaceForTestIndexedSetterThrowingException.get(); },
        [] (auto& spaces, auto&& space) { spaces.m_subspaceForTestIndexedSetterThrowingException = std::forward<decltype(space)>(space); }
    );
}

void JSTestIndexedSetterThrowingException::analyzeHeap(JSCell* cell, HeapAnalyzer& analyzer)
{
    auto* thisObject = jsCast<JSTestIndexedSetterThrowingException*>(cell);
    analyzer.setWrappedObjectForCell(cell, &thisObject->wrapped());
    if (RefPtr context = thisObject->scriptExecutionContext())
        analyzer.setLabelForCell(cell, makeString("url "_s, context->url().string()));
    Base::analyzeHeap(cell, analyzer);
}

bool JSTestIndexedSetterThrowingExceptionOwner::isReachableFromOpaqueRoots(JSC::Handle<JSC::Unknown> handle, void*, AbstractSlotVisitor& visitor, ASCIILiteral* reason)
{
    UNUSED_PARAM(handle);
    UNUSED_PARAM(visitor);
    UNUSED_PARAM(reason);
    return false;
}

void JSTestIndexedSetterThrowingExceptionOwner::finalize(JSC::Handle<JSC::Unknown> handle, void* context)
{
    auto* jsTestIndexedSetterThrowingException = static_cast<JSTestIndexedSetterThrowingException*>(handle.slot()->asCell());
    auto& world = *static_cast<DOMWrapperWorld*>(context);
    uncacheWrapper(world, jsTestIndexedSetterThrowingException->protectedWrapped().ptr(), jsTestIndexedSetterThrowingException);
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN
#if ENABLE(BINDING_INTEGRITY)
#if PLATFORM(WIN)
#pragma warning(disable: 4483)
extern "C" { extern void (*const __identifier("??_7TestIndexedSetterThrowingException@WebCore@@6B@")[])(); }
#else
extern "C" { extern void* _ZTVN7WebCore34TestIndexedSetterThrowingExceptionE[]; }
#endif
template<std::same_as<TestIndexedSetterThrowingException> T>
static inline void verifyVTable(TestIndexedSetterThrowingException* ptr) 
{
    if constexpr (std::is_polymorphic_v<T>) {
        const void* actualVTablePointer = getVTablePointer<T>(ptr);
#if PLATFORM(WIN)
        void* expectedVTablePointer = __identifier("??_7TestIndexedSetterThrowingException@WebCore@@6B@");
#else
        void* expectedVTablePointer = &_ZTVN7WebCore34TestIndexedSetterThrowingExceptionE[2];
#endif

        // If you hit this assertion you either have a use after free bug, or
        // TestIndexedSetterThrowingException has subclasses. If TestIndexedSetterThrowingException has subclasses that get passed
        // to toJS() we currently require TestIndexedSetterThrowingException you to opt out of binding hardening
        // by adding the SkipVTableValidation attribute to the interface IDL definition
        RELEASE_ASSERT(actualVTablePointer == expectedVTablePointer);
    }
}
#endif
WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

JSC::JSValue toJSNewlyCreated(JSC::JSGlobalObject*, JSDOMGlobalObject* globalObject, Ref<TestIndexedSetterThrowingException>&& impl)
{
#if ENABLE(BINDING_INTEGRITY)
    verifyVTable<TestIndexedSetterThrowingException>(impl.ptr());
#endif
    return createWrapper<TestIndexedSetterThrowingException>(globalObject, WTFMove(impl));
}

JSC::JSValue toJS(JSC::JSGlobalObject* lexicalGlobalObject, JSDOMGlobalObject* globalObject, TestIndexedSetterThrowingException& impl)
{
    return wrap(lexicalGlobalObject, globalObject, impl);
}

TestIndexedSetterThrowingException* JSTestIndexedSetterThrowingException::toWrapped(JSC::VM&, JSC::JSValue value)
{
    if (auto* wrapper = jsDynamicCast<JSTestIndexedSetterThrowingException*>(value))
        return &wrapper->wrapped();
    return nullptr;
}

}
