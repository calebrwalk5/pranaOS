/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// includes
#include <base/Function.h>
#include <libjs/Interpreter.h>
#include <libjs/runtime/AbstractOperations.h>
#include <libjs/runtime/Error.h>
#include <libjs/runtime/GlobalObject.h>
#include <libjs/runtime/Promise.h>
#include <libjs/runtime/PromiseConstructor.h>
#include <libjs/runtime/PromisePrototype.h>
#include <libjs/runtime/PromiseReaction.h>

namespace JS {

PromisePrototype::PromisePrototype(GlobalObject& global_object)
    : Object(*global_object.object_prototype())
{
}

void PromisePrototype::initialize(GlobalObject& global_object)
{
    auto& vm = this->vm();
    Object::initialize(global_object);

    u8 attr = Attribute::Writable | Attribute::Configurable;
    define_native_function(vm.names.then, then, 2, attr);
    define_native_function(vm.names.catch_, catch_, 1, attr);
    define_native_function(vm.names.finally, finally, 1, attr);

    define_direct_property(*vm.well_known_symbol_to_string_tag(), js_string(vm, vm.names.Promise.as_string()), Attribute::Configurable);
}

static Promise* promise_from(VM& vm, GlobalObject& global_object)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return nullptr;
    if (!is<Promise>(this_object)) {
        vm.throw_exception<TypeError>(global_object, ErrorType::NotA, vm.names.Promise);
        return nullptr;
    }
    return static_cast<Promise*>(this_object);
}

JS_DEFINE_NATIVE_FUNCTION(PromisePrototype::then)
{
    auto* promise = promise_from(vm, global_object);
    if (!promise)
        return {};
    auto on_fulfilled = vm.argument(0);
    auto on_rejected = vm.argument(1);
    auto* constructor = species_constructor(global_object, *promise, *global_object.promise_constructor());
    if (vm.exception())
        return {};
    auto result_capability = new_promise_capability(global_object, constructor);
    if (vm.exception())
        return {};
    return promise->perform_then(on_fulfilled, on_rejected, result_capability);
}

JS_DEFINE_NATIVE_FUNCTION(PromisePrototype::catch_)
{
    auto this_value = vm.this_value(global_object);
    auto on_rejected = vm.argument(0);
    return this_value.invoke(global_object, vm.names.then, js_undefined(), on_rejected);
}

JS_DEFINE_NATIVE_FUNCTION(PromisePrototype::finally)
{
    auto* promise = vm.this_value(global_object).to_object(global_object);
    if (!promise)
        return {};
    auto* constructor = species_constructor(global_object, *promise, *global_object.promise_constructor());
    if (vm.exception())
        return {};
    Value then_finally;
    Value catch_finally;
    auto on_finally = vm.argument(0);
    if (!on_finally.is_function()) {
        then_finally = on_finally;
        catch_finally = on_finally;
    } else {

        auto* then_finally_function = NativeFunction::create(global_object, "", [constructor_handle = make_handle(constructor), on_finally_handle = make_handle(&on_finally.as_function())](auto& vm, auto& global_object) -> Value {
            auto& constructor = const_cast<FunctionObject&>(*constructor_handle.cell());
            auto& on_finally = const_cast<FunctionObject&>(*on_finally_handle.cell());
            auto value = vm.argument(0);
            auto result = vm.call(on_finally, js_undefined());
            if (vm.exception())
                return {};
            auto* promise = promise_resolve(global_object, constructor, result);
            if (vm.exception())
                return {};
            auto* value_thunk = NativeFunction::create(global_object, "", [value](auto&, auto&) -> Value {
                return value;
            });
            return Value(promise).invoke(global_object, vm.names.then, value_thunk);
        });
        then_finally_function->define_direct_property(vm.names.length, Value(1), Attribute::Configurable);

        auto* catch_finally_function = NativeFunction::create(global_object, "", [constructor_handle = make_handle(constructor), on_finally_handle = make_handle(&on_finally.as_function())](auto& vm, auto& global_object) -> Value {
            auto& constructor = const_cast<FunctionObject&>(*constructor_handle.cell());
            auto& on_finally = const_cast<FunctionObject&>(*on_finally_handle.cell());
            auto reason = vm.argument(0);
            auto result = vm.call(on_finally, js_undefined());
            if (vm.exception())
                return {};
            auto* promise = promise_resolve(global_object, constructor, result);
            if (vm.exception())
                return {};
            auto* thrower = NativeFunction::create(global_object, "", [reason](auto& vm, auto& global_object) -> Value {
                vm.throw_exception(global_object, reason);
                return {};
            });
            return Value(promise).invoke(global_object, vm.names.then, thrower);
        });
        catch_finally_function->define_direct_property(vm.names.length, Value(1), Attribute::Configurable);

        then_finally = Value(then_finally_function);
        catch_finally = Value(catch_finally_function);
    }
    return Value(promise).invoke(global_object, vm.names.then, then_finally, catch_finally);
}

}