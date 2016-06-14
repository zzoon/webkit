/*
 * Copyright (C) 2016 Canon, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY CANON INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CANON INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef JSDOMIterator_h
#define JSDOMIterator_h

#include "JSDOMBinding.h"
#include <runtime/JSDestructibleObject.h>
#include <type_traits>

namespace WebCore {

template<typename JSWrapper>
class JSDOMIteratorPrototype : public JSC::JSNonFinalObject {
public:
    using Base = JSC::JSNonFinalObject;
    using DOMWrapped = typename JSWrapper::DOMWrapped;

    static JSDOMIteratorPrototype* create(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::Structure* structure)
    {
        JSDOMIteratorPrototype* prototype = new (NotNull, JSC::allocateCell<JSDOMIteratorPrototype>(vm.heap)) JSDOMIteratorPrototype(vm, structure);
        prototype->finishCreation(vm, globalObject);
        return prototype;
    }

    DECLARE_INFO;

    static JSC::Structure* createStructure(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::JSValue prototype)
    {
        return JSC::Structure::create(vm, globalObject, prototype, JSC::TypeInfo(JSC::ObjectType, StructureFlags), info());
    }

    static JSC::EncodedJSValue JSC_HOST_CALL next(JSC::ExecState*);

private:
    JSDOMIteratorPrototype(JSC::VM& vm, JSC::Structure* structure) : Base(vm, structure) { }

    void finishCreation(JSC::VM&, JSC::JSGlobalObject*);
};

template<typename IteratorValue>
class IteratorInspector {
private:
    template<typename T> static constexpr auto test(int) -> decltype(std::declval<T>()->key, std::declval<T>()->value, bool()) { return true; }
    template<typename T> static constexpr bool test(...) { return false; }
public:
    static constexpr bool isMap = test<IteratorValue>(0);
    static constexpr bool isSet = !isMap;
};

enum class IterationKind { Key, Value, KeyValue };

template<typename JSWrapper>
class JSDOMIterator: public JSDOMObject {
public:
    using DOMWrapped = typename std::remove_reference<decltype(std::declval<JSWrapper>().wrapped())>::type;
    using Base = JSDOMObject;

    DECLARE_INFO;

    static JSC::Structure* createStructure(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::JSValue prototype)
    {
        return JSC::Structure::create(vm, globalObject, prototype, JSC::TypeInfo(JSC::ObjectType, StructureFlags), info());
    }

    static JSDOMIterator* create(JSC::VM& vm, JSC::Structure* structure, JSWrapper& iteratedObject, IterationKind kind)
    {
        JSDOMIterator* instance = new (NotNull, JSC::allocateCell<JSDOMIterator>(vm.heap)) JSDOMIterator(structure, iteratedObject, kind);
        instance->finishCreation(vm);
        return instance;
    }

    static JSDOMIteratorPrototype<JSWrapper>* createPrototype(JSC::VM& vm, JSC::JSGlobalObject* globalObject)
    {
        return JSDOMIteratorPrototype<JSWrapper>::create(vm, globalObject,
            JSDOMIteratorPrototype<JSWrapper>::createStructure(vm, globalObject, globalObject->objectPrototype()));
    }

    JSC::JSValue next(JSC::ExecState&);

private:
    JSDOMIterator(JSC::Structure* structure, JSWrapper& iteratedObject, IterationKind kind)
        : Base(structure, *iteratedObject.globalObject())
        , m_iterator(iteratedObject.wrapped().createIterator())
        , m_kind(kind)
    {
    }

    template<typename IteratorValue> typename std::enable_if<IteratorInspector<IteratorValue>::isMap, JSC::JSValue>::type
    asJS(JSC::ExecState&, IteratorValue&);
    template<typename IteratorValue> typename std::enable_if<IteratorInspector<IteratorValue>::isSet, JSC::JSValue>::type
    asJS(JSC::ExecState&, IteratorValue&);

    static void destroy(JSC::JSCell*);

    Optional<typename DOMWrapped::Iterator> m_iterator;
    IterationKind m_kind;
    size_t m_index { 0 };
};

template<typename JSWrapper>
JSC::EncodedJSValue iteratorCreate(JSC::ExecState&, IterationKind, const char*);
template<typename JSWrapper>
JSC::EncodedJSValue iteratorForEach(JSC::ExecState&, const char*);

template<typename JSWrapper>
JSC::EncodedJSValue iteratorCreate(JSC::ExecState& state, IterationKind kind, const char* propertyName)
{
    auto wrapper = JSC::jsDynamicCast<JSWrapper*>(state.thisValue());
    if (UNLIKELY(!wrapper))
        return throwThisTypeError(state, JSWrapper::info()->className, propertyName);
    JSDOMGlobalObject& globalObject = *wrapper->globalObject();
    return JSC::JSValue::encode(JSDOMIterator<JSWrapper>::create(globalObject.vm(), getDOMStructure<JSDOMIterator<JSWrapper>>(globalObject.vm(), globalObject), *wrapper, kind));
}

template<typename JSWrapper>
template<typename IteratorValue> inline typename std::enable_if<IteratorInspector<IteratorValue>::isMap, JSC::JSValue>::type
JSDOMIterator<JSWrapper>::asJS(JSC::ExecState& state, IteratorValue& value)
{
    ASSERT(value);
    if (m_kind != IterationKind::KeyValue)
        return toJS(&state, globalObject(), (m_kind == IterationKind::Key) ? value->key : value->value);

    return jsPair(state, globalObject(), value->key, value->value);
}

template<typename JSWrapper>
template<typename IteratorValue> inline typename std::enable_if<IteratorInspector<IteratorValue>::isSet, JSC::JSValue>::type
JSDOMIterator<JSWrapper>::asJS(JSC::ExecState& state, IteratorValue& value)
{
    ASSERT(value);
    JSC::JSValue result = toJS(&state, globalObject(), *value);
    if (m_kind != IterationKind::KeyValue)
        return result;

    return jsPair(state, globalObject(), JSC::jsNumber(m_index++), result);
}

template<typename IteratorValue> typename std::enable_if<IteratorInspector<IteratorValue>::isMap, void>::type
appendForEachArguments(JSC::ExecState& state, JSDOMGlobalObject* globalObject, JSC::MarkedArgumentBuffer& arguments, IteratorValue& value, size_t&)
{
    ASSERT(value);
    arguments.append(toJS(&state, globalObject, value->value));
    arguments.append(toJS(&state, globalObject, value->key));
}

template<typename IteratorValue> typename std::enable_if<IteratorInspector<IteratorValue>::isSet, void>::type
appendForEachArguments(JSC::ExecState& state, JSDOMGlobalObject* globalObject, JSC::MarkedArgumentBuffer& arguments, IteratorValue& value, size_t& index)
{
    ASSERT(value);
    JSC::JSValue argument = toJS(&state, globalObject, *value);
    arguments.append(argument);
    arguments.append(JSC::jsNumber(index++));
}

template<typename JSWrapper>
JSC::EncodedJSValue iteratorForEach(JSC::ExecState& state, const char* propertyName)
{
    auto wrapper = JSC::jsDynamicCast<JSWrapper*>(state.thisValue());
    if (UNLIKELY(!wrapper))
        return throwThisTypeError(state, JSWrapper::info()->className, propertyName);

    JSC::CallData callData;
    JSC::CallType callType = JSC::getCallData(state.argument(0), callData);
    if (callType == JSC::CallType::None)
        return throwVMTypeError(&state);

    size_t index = 0;
    auto iterator = wrapper->wrapped().createIterator();
    while (auto value = iterator.next()) {
        JSC::MarkedArgumentBuffer arguments;
        appendForEachArguments(state, wrapper->globalObject(), arguments, value, index);
        arguments.append(wrapper);
        JSC::call(&state, state.argument(0), callType, callData, wrapper, arguments);
        if (state.hadException())
            break;
    } 
    return JSC::JSValue::encode(JSC::jsUndefined());
}

template<typename JSWrapper>
void JSDOMIterator<JSWrapper>::destroy(JSCell* cell)
{
    JSDOMIterator<JSWrapper>* thisObject = JSC::jsCast<JSDOMIterator<JSWrapper>*>(cell);
    thisObject->JSDOMIterator<JSWrapper>::~JSDOMIterator();
}

template<typename JSWrapper>
JSC::JSValue JSDOMIterator<JSWrapper>::next(JSC::ExecState& state)
{
    if (m_iterator) {
        auto iteratorValue = m_iterator->next();
        if (iteratorValue)
            return createIteratorResultObject(&state, asJS(state, iteratorValue), false);
        m_iterator = Nullopt;
    }
    return createIteratorResultObject(&state, JSC::jsUndefined(), true);
}

template<typename JSWrapper>
JSC::EncodedJSValue JSC_HOST_CALL JSDOMIteratorPrototype<JSWrapper>::next(JSC::ExecState* state)
{
    auto iterator = JSC::jsDynamicCast<JSDOMIterator<JSWrapper>*>(state->thisValue());
    if (!iterator)
        return JSC::JSValue::encode(throwTypeError(state, ASCIILiteral("Cannot call next() on a non-Iterator object")));

    return JSC::JSValue::encode(iterator->next(*state));
}

template<typename JSWrapper>
void JSDOMIteratorPrototype<JSWrapper>::finishCreation(JSC::VM& vm, JSC::JSGlobalObject* globalObject)
{
    Base::finishCreation(vm);
    ASSERT(inherits(info()));

    JSC_NATIVE_INTRINSIC_FUNCTION_WITHOUT_TRANSITION(vm.propertyNames->next, next, JSC::DontEnum, 0, JSC::NoIntrinsic);
}

}

#endif // !defined(JSDOMIterator_h)
