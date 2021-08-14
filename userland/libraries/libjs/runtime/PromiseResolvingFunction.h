/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <libjs/runtime/NativeFunction.h>

namespace JS {

struct AlreadyResolved final : public Cell {
    bool value { false };

    virtual const char* class_name() const override { return "AlreadyResolved"; }

protected:

    u8 dummy[8];
};

class PromiseResolvingFunction final : public NativeFunction {
    JS_OBJECT(PromiseResolvingFunction, NativeFunction);

public:
    using FunctionType = Function<Value(VM&, GlobalObject&, Promise&, AlreadyResolved&)>;

    static PromiseResolvingFunction* create(GlobalObject&, Promise&, AlreadyResolved&, FunctionType);

    explicit PromiseResolvingFunction(Promise&, AlreadyResolved&, FunctionType, Object& prototype);
    virtual void initialize(GlobalObject&) override;
    virtual ~PromiseResolvingFunction() override = default;

    virtual Value call() override;

private:
    virtual void visit_edges(Visitor&) override;

    Promise& m_promise;
    AlreadyResolved& m_already_resolved;
    FunctionType m_native_function;
};

}