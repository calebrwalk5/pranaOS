/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <libjs/runtime/Object.h>
#include <libweb/Forward.h>

namespace Web {
namespace Bindings {

class NavigatorObject final : public JS::Object {
    JS_OBJECT(NavigatorObject, JS::Object);

public:
    NavigatorObject(JS::GlobalObject&);
    virtual void initialize(JS::GlobalObject&) override;
    virtual ~NavigatorObject() override;

private:
    JS_DECLARE_NATIVE_FUNCTION(user_agent_getter);
};

}
}