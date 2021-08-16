/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// includes
#include <base/Assertions.h>
#include <base/TypeCasts.h>
#include <libjs/runtime/FunctionObject.h>
#include <libweb/bindings/EventTargetWrapper.h>
#include <libweb/bindings/EventTargetWrapperFactory.h>
#include <libweb/bindings/EventWrapper.h>
#include <libweb/bindings/EventWrapperFactory.h>
#include <libweb/bindings/ScriptExecutionContext.h>
#include <libweb/bindings/WindowObject.h>
#include <libweb/dom/Document.h>
#include <libweb/dom/Event.h>
#include <libweb/dom/EventDispatcher.h>
#include <libweb/dom/EventListener.h>
#include <libweb/dom/EventTarget.h>
#include <libweb/dom/Node.h>
#include <libweb/dom/ShadowRoot.h>
#include <libweb/dom/Window.h>
#include <libweb/html/EventNames.h>
#include <libweb/uievents/MouseEvent.h>

namespace Web::DOM {

static EventTarget* retarget(EventTarget* left, [[maybe_unused]] EventTarget* right)
{
    for (;;) {
        if (!is<Node>(left))
            return left;

        auto* left_node = verify_cast<Node>(left);
        auto* left_root = left_node->root();
        if (!is<ShadowRoot>(left_root))
            return left;


        auto* left_shadow_root = verify_cast<ShadowRoot>(left_root);
        left = left_shadow_root->host();
    }
}

bool EventDispatcher::inner_invoke(Event& event, Vector<EventTarget::EventListenerRegistration>& listeners, Event::Phase phase, bool invocation_target_in_shadow_tree)
{
    bool found = false;

    for (auto& listener : listeners) {
        if (listener.listener->removed())
            continue;

        if (event.type() != listener.listener->type())
            continue;

        found = true;

        if (phase == Event::Phase::CapturingPhase && !listener.listener->capture())
            continue;

        if (phase == Event::Phase::BubblingPhase && listener.listener->capture())
            continue;

        if (listener.listener->once())
            event.current_target()->remove_from_event_listener_list(listener.listener);

        auto& function = listener.listener->function();
        auto& global = function.global_object();

        RefPtr<Event> current_event;

        if (is<Bindings::WindowObject>(global)) {
            auto& bindings_window_global = verify_cast<Bindings::WindowObject>(global);
            auto& window_impl = bindings_window_global.impl();
            current_event = window_impl.current_event();
            if (!invocation_target_in_shadow_tree)
                window_impl.set_current_event(&event);
        }

        if (listener.listener->passive())
            event.set_in_passive_listener(true);

        auto* this_value = Bindings::wrap(global, *event.current_target());
        auto* wrapped_event = Bindings::wrap(global, event);
        auto& vm = global.vm();
        [[maybe_unused]] auto rc = vm.call(listener.listener->function(), this_value, wrapped_event);
        if (vm.exception()) {
            vm.clear_exception();

        }

        event.set_in_passive_listener(false);
        if (is<Bindings::WindowObject>(global)) {
            auto& bindings_window_global = verify_cast<Bindings::WindowObject>(global);
            auto& window_impl = bindings_window_global.impl();
            window_impl.set_current_event(current_event);
        }

        if (event.should_stop_immediate_propagation())
            return found;
    }

    return found;
}

void EventDispatcher::invoke(Event::PathEntry& struct_, Event& event, Event::Phase phase)
{
    auto last_valid_shadow_adjusted_target = event.path().last_matching([&struct_](auto& entry) {
        return entry.index <= struct_.index && !entry.shadow_adjusted_target.is_null();
    });

    VERIFY(last_valid_shadow_adjusted_target.has_value());

    event.set_target(last_valid_shadow_adjusted_target.value().shadow_adjusted_target);
    event.set_related_target(struct_.related_target);
    event.set_touch_target_list(struct_.touch_target_list);

    if (event.should_stop_propagation())
        return;

    event.set_current_target(struct_.invocation_target);

    auto listeners = event.current_target()->listeners();
    bool invocation_target_in_shadow_tree = struct_.invocation_target_in_shadow_tree;

    bool found = inner_invoke(event, listeners, phase, invocation_target_in_shadow_tree);

    if (!found && event.is_trusted()) {
        auto original_event_type = event.type();

        if (event.type() == "animationend")
            event.set_type("webkitAnimationEnd");
        else if (event.type() == "animationiteration")
            event.set_type("webkitAnimationIteration");
        else if (event.type() == "animationstart")
            event.set_type("webkitAnimationStart");
        else if (event.type() == "transitionend")
            event.set_type("webkitTransitionEnd");
        else
            return;

        inner_invoke(event, listeners, phase, invocation_target_in_shadow_tree);
        event.set_type(original_event_type);
    }
}

bool EventDispatcher::dispatch(NonnullRefPtr<EventTarget> target, NonnullRefPtr<Event> event, bool legacy_target_override)
{
    event->set_dispatched(true);
    RefPtr<EventTarget> target_override;

    if (!legacy_target_override) {
        target_override = target;
    } else {

        target_override = verify_cast<Window>(*target).document();
    }

    RefPtr<EventTarget> activation_target;
    RefPtr<EventTarget> related_target = retarget(event->related_target(), target);

    bool clear_targets = false;

    if (related_target != target || event->related_target() == target) {
        Event::TouchTargetList touch_targets;

        for (auto& touch_target : event->touch_target_list()) {
            touch_targets.append(retarget(touch_target, target));
        }

        event->append_to_path(*target, target_override, related_target, touch_targets, false);

        bool is_activation_event = is<UIEvents::MouseEvent>(*event) && event->type() == HTML::EventNames::click;

        if (is_activation_event && target->activation_behaviour)
            activation_target = target;


        bool slot_in_closed_tree = false;
        auto* parent = target->get_parent(event);

        while (parent) {

            related_target = retarget(event->related_target(), parent);
            touch_targets.clear();

            for (auto& touch_target : event->touch_target_list()) {
                touch_targets.append(retarget(touch_target, parent));
            }

            if (is<Window>(parent)) {
                if (is_activation_event && event->bubbles() && !activation_target && parent->activation_behaviour)
                    activation_target = parent;

                event->append_to_path(*parent, nullptr, related_target, touch_targets, slot_in_closed_tree);
            } else if (related_target == parent) {
                parent = nullptr;
            } else {
                target = *parent;

                if (is_activation_event && !activation_target && target->activation_behaviour)
                    activation_target = target;

                event->append_to_path(*parent, target, related_target, touch_targets, slot_in_closed_tree);
            }

            if (parent) {
                parent = parent->get_parent(event);
            }

            slot_in_closed_tree = false;
        }

        auto clear_targets_struct = event->path().last_matching([](auto& entry) {
            return !entry.shadow_adjusted_target.is_null();
        });

        VERIFY(clear_targets_struct.has_value());

        if (is<Node>(clear_targets_struct.value().shadow_adjusted_target.ptr())) {
            auto& shadow_adjusted_target_node = verify_cast<Node>(*clear_targets_struct.value().shadow_adjusted_target);
            if (is<ShadowRoot>(shadow_adjusted_target_node.root()))
                clear_targets = true;
        }

        if (!clear_targets && is<Node>(clear_targets_struct.value().related_target.ptr())) {
            auto& related_target_node = verify_cast<Node>(*clear_targets_struct.value().related_target);
            if (is<ShadowRoot>(related_target_node.root()))
                clear_targets = true;
        }

        if (!clear_targets) {
            for (auto touch_target : clear_targets_struct.value().touch_target_list) {
                if (is<Node>(*touch_target.ptr())) {
                    auto& touch_target_node = verify_cast<Node>(*touch_target.ptr());
                    if (is<ShadowRoot>(touch_target_node.root())) {
                        clear_targets = true;
                        break;
                    }
                }
            }
        }

        if (activation_target && activation_target->legacy_pre_activation_behaviour)
            activation_target->legacy_pre_activation_behaviour();

        for (ssize_t i = event->path().size() - 1; i >= 0; --i) {
            auto& entry = event->path().at(i);

            if (entry.shadow_adjusted_target)
                event->set_phase(Event::Phase::AtTarget);
            else
                event->set_phase(Event::Phase::CapturingPhase);

            invoke(entry, event, Event::Phase::CapturingPhase);
        }

        for (auto& entry : event->path()) {
            if (entry.shadow_adjusted_target) {
                event->set_phase(Event::Phase::AtTarget);
            } else {
                if (!event->bubbles())
                    continue;

                event->set_phase(Event::Phase::BubblingPhase);
            }

            invoke(entry, event, Event::Phase::BubblingPhase);
        }
    }

    event->set_phase(Event::Phase::None);
    event->set_current_target(nullptr);
    event->clear_path();
    event->set_dispatched(false);
    event->set_stop_propagation(false);
    event->set_stop_immediate_propagation(false);

    if (clear_targets) {
        event->set_target(nullptr);
        event->set_related_target(nullptr);
        event->clear_touch_target_list();
    }

    if (activation_target) {
        if (!event->cancelled()) {
            
            activation_target->activation_behaviour(event);
        } else {
            if (activation_target->legacy_cancelled_activation_behaviour)
                activation_target->legacy_cancelled_activation_behaviour();
        }
    }

    return !event->cancelled();
}

}