/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// includes
#include <libjs/Interpreter.h>
#include <libjs/Parser.h>
#include <libjs/runtime/OrdinaryFunctionObject.h>
#include <libweb/dom/Document.h>
#include <libweb/dom/EventListener.h>
#include <libweb/html/EventHandler.h>
#include <libweb/html/EventNames.h>
#include <libweb/html/GlobalEventHandlers.h>
#include <libweb/uievents/EventNames.h>

namespace Web::HTML {

#undef __ENUMERATE
#define __ENUMERATE(attribute_name, event_name)                              \
    void GlobalEventHandlers::set_##attribute_name(HTML::EventHandler value) \
    {                                                                        \
        set_event_handler_attribute(event_name, move(value));                \
    }                                                                        \
    HTML::EventHandler GlobalEventHandlers::attribute_name()                 \
    {                                                                        \
        return get_event_handler_attribute(event_name);                      \
    }
ENUMERATE_GLOBAL_EVENT_HANDLERS(__ENUMERATE)
#undef __ENUMERATE

GlobalEventHandlers::~GlobalEventHandlers()
{
}

void GlobalEventHandlers::set_event_handler_attribute(const FlyString& name, HTML::EventHandler value)
{
    auto& self = global_event_handlers_to_event_target();

    RefPtr<DOM::EventListener> listener;
    if (!value.callback.is_null()) {
        listener = adopt_ref(*new DOM::EventListener(move(value.callback)));
    } else {
        StringBuilder builder;
        builder.appendff("function {}(event) {{\n{}\n}}", name, value.string);
        auto parser = JS::Parser(JS::Lexer(builder.string_view()));
        auto program = parser.parse_function_node<JS::FunctionExpression>();
        if (parser.has_errors()) {
            dbgln("Failed to parse script in event handler attribute '{}'", name);
            return;
        }
        auto* function = JS::OrdinaryFunctionObject::create(self.script_execution_context()->interpreter().global_object(), name, program->body(), program->parameters(), program->function_length(), nullptr, JS::FunctionKind::Regular, false, false);
        VERIFY(function);
        listener = adopt_ref(*new DOM::EventListener(JS::make_handle(static_cast<JS::FunctionObject*>(function))));
    }
    if (listener) {
        for (auto& registered_listener : self.listeners()) {
            if (registered_listener.event_name == name && registered_listener.listener->is_attribute()) {
                self.remove_event_listener(name, registered_listener.listener);
                break;
            }
        }
        self.add_event_listener(name, listener.release_nonnull());
    }
}

HTML::EventHandler GlobalEventHandlers::get_event_handler_attribute(const FlyString& name)
{
    auto& self = global_event_handlers_to_event_target();
    for (auto& listener : self.listeners()) {
        if (listener.event_name == name && listener.listener->is_attribute()) {
            return HTML::EventHandler { JS::make_handle(&listener.listener->function()) };
        }
    }

    return {};
}

}