/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// includes
#include <libgui/Event.h>
#include <libgui/Window.h>
#include <libweb/dom/Range.h>
#include <libweb/dom/Text.h>
#include <libweb/html/HTMLAnchorElement.h>
#include <libweb/html/HTMLIFrameElement.h>
#include <libweb/html/HTMLImageElement.h>
#include <libweb/InProcessWebView.h>
#include <libweb/layout/InitialContainingBlockBox.h>
#include <libweb/page/BrowsingContext.h>
#include <libweb/page/EventHandler.h>
#include <libweb/uievents/EventNames.h>
#include <libweb/uievents/MouseEvent.h>

namespace Web {

static Gfx::StandardCursor cursor_css_to_gfx(Optional<CSS::Cursor> cursor)
{
    if (!cursor.has_value()) {
        return Gfx::StandardCursor::None;
    }
    switch (cursor.value()) {
    case CSS::Cursor::Crosshair:
    case CSS::Cursor::Cell:
        return Gfx::StandardCursor::Crosshair;
    case CSS::Cursor::Grab:
    case CSS::Cursor::Grabbing:
        return Gfx::StandardCursor::Drag;
    case CSS::Cursor::Pointer:
        return Gfx::StandardCursor::Hand;
    case CSS::Cursor::Help:
        return Gfx::StandardCursor::Help;
    case CSS::Cursor::None:
        return Gfx::StandardCursor::Hidden;
    case CSS::Cursor::Text:
    case CSS::Cursor::VerticalText:
        return Gfx::StandardCursor::IBeam;
    case CSS::Cursor::Move:
    case CSS::Cursor::AllScroll:
        return Gfx::StandardCursor::Move;
    case CSS::Cursor::Progress:
    case CSS::Cursor::Wait:
        return Gfx::StandardCursor::Wait;

    case CSS::Cursor::ColResize:
        return Gfx::StandardCursor::ResizeColumn;
    case CSS::Cursor::EResize:
    case CSS::Cursor::WResize:
    case CSS::Cursor::EwResize:
        return Gfx::StandardCursor::ResizeHorizontal;

    case CSS::Cursor::RowResize:
        return Gfx::StandardCursor::ResizeRow;
    case CSS::Cursor::NResize:
    case CSS::Cursor::SResize:
    case CSS::Cursor::NsResize:
        return Gfx::StandardCursor::ResizeVertical;

    case CSS::Cursor::NeResize:
    case CSS::Cursor::SwResize:
    case CSS::Cursor::NeswResize:
        return Gfx::StandardCursor::ResizeDiagonalBLTR;

    case CSS::Cursor::NwResize:
    case CSS::Cursor::SeResize:
    case CSS::Cursor::NwseResize:
        return Gfx::StandardCursor::ResizeDiagonalTLBR;

    default:
        return Gfx::StandardCursor::None;
    }
}

static Gfx::IntPoint compute_mouse_event_offset(const Gfx::IntPoint& position, const Layout::Node& layout_node)
{
    auto top_left_of_layout_node = layout_node.box_type_agnostic_position();
    return {
        position.x() - static_cast<int>(top_left_of_layout_node.x()),
        position.y() - static_cast<int>(top_left_of_layout_node.y())
    };
}

EventHandler::EventHandler(Badge<BrowsingContext>, BrowsingContext& frame)
    : m_frame(frame)
    , m_edit_event_handler(make<EditEventHandler>(frame))
{
}

EventHandler::~EventHandler()
{
}

const Layout::InitialContainingBlockBox* EventHandler::layout_root() const
{
    if (!m_frame.document())
        return nullptr;
    return m_frame.document()->layout_node();
}

Layout::InitialContainingBlockBox* EventHandler::layout_root()
{
    if (!m_frame.document())
        return nullptr;
    return m_frame.document()->layout_node();
}

bool EventHandler::handle_mousewheel(const Gfx::IntPoint& position, unsigned int buttons, unsigned int modifiers, int wheel_delta)
{
    if (!layout_root())
        return false;


    auto result = layout_root()->hit_test(position, Layout::HitTestType::Exact);
    if (result.layout_node) {
        if (result.layout_node->handle_mousewheel({}, position, buttons, modifiers, wheel_delta))
            return true;
    }

    if (auto* page = m_frame.page()) {
        page->client().page_did_request_scroll(wheel_delta);
        return true;
    }

    return false;
}

bool EventHandler::handle_mouseup(const Gfx::IntPoint& position, unsigned button, unsigned modifiers)
{
    if (!layout_root())
        return false;

    if (m_mouse_event_tracking_layout_node) {
        m_mouse_event_tracking_layout_node->handle_mouseup({}, position, button, modifiers);
        return true;
    }

    bool handled_event = false;

    auto result = layout_root()->hit_test(position, Layout::HitTestType::Exact);

    if (result.layout_node && result.layout_node->wants_mouse_events()) {
        result.layout_node->handle_mouseup({}, position, button, modifiers);

        if (!layout_root())
            return true;
        result = layout_root()->hit_test(position, Layout::HitTestType::Exact);
    }

    if (result.layout_node && result.layout_node->dom_node()) {
        RefPtr<DOM::Node> node = result.layout_node->dom_node();
        if (is<HTML::HTMLIFrameElement>(*node)) {
            if (auto* subframe = verify_cast<HTML::HTMLIFrameElement>(*node).nested_browsing_context())
                return subframe->event_handler().handle_mouseup(position.translated(compute_mouse_event_offset({}, *result.layout_node)), button, modifiers);
            return false;
        }
        auto offset = compute_mouse_event_offset(position, *result.layout_node);
        node->dispatch_event(UIEvents::MouseEvent::create(UIEvents::EventNames::mouseup, offset.x(), offset.y(), position.x(), position.y()));
        handled_event = true;
    }

    if (button == GUI::MouseButton::Left)
        m_in_mouse_selection = false;
    return handled_event;
}

bool EventHandler::handle_mousedown(const Gfx::IntPoint& position, unsigned button, unsigned modifiers)
{
    if (!layout_root())
        return false;

    if (m_mouse_event_tracking_layout_node) {
        m_mouse_event_tracking_layout_node->handle_mousedown({}, position, button, modifiers);
        return true;
    }

    NonnullRefPtr document = *m_frame.document();
    RefPtr<DOM::Node> node;

    {
        auto result = layout_root()->hit_test(position, Layout::HitTestType::Exact);
        if (!result.layout_node)
            return false;

        node = result.layout_node->dom_node();
        document->set_hovered_node(node);

        if (result.layout_node->wants_mouse_events()) {
            result.layout_node->handle_mousedown({}, position, button, modifiers);
            return true;
        }

        if (!node)
            return false;

        if (is<HTML::HTMLIFrameElement>(*node)) {
            if (auto* subframe = verify_cast<HTML::HTMLIFrameElement>(*node).nested_browsing_context())
                return subframe->event_handler().handle_mousedown(position.translated(compute_mouse_event_offset({}, *result.layout_node)), button, modifiers);
            return false;
        }

        if (auto* page = m_frame.page())
            page->set_focused_browsing_context({}, m_frame);

        auto offset = compute_mouse_event_offset(position, *result.layout_node);
        node->dispatch_event(UIEvents::MouseEvent::create(UIEvents::EventNames::mousedown, offset.x(), offset.y(), position.x(), position.y()));
    }

    if (!layout_root() || layout_root() != node->document().layout_node())
        return true;

    if (button == GUI::MouseButton::Right && is<HTML::HTMLImageElement>(*node)) {
        auto& image_element = verify_cast<HTML::HTMLImageElement>(*node);
        auto image_url = image_element.document().complete_url(image_element.src());
        if (auto* page = m_frame.page())
            page->client().page_did_request_image_context_menu(m_frame.to_top_level_position(position), image_url, "", modifiers, image_element.bitmap());
        return true;
    }

    if (RefPtr<HTML::HTMLAnchorElement> link = node->enclosing_link_element()) {
        auto href = link->href();
        auto url = document->complete_url(href);
        dbgln("Web::EventHandler: Clicking on a link to {}", url);
        if (button == GUI::MouseButton::Left) {
            if (href.starts_with("javascript:")) {
                document->run_javascript(href.substring_view(11, href.length() - 11));
            } else if (href.starts_with('#')) {
                auto anchor = href.substring_view(1, href.length() - 1);
                m_frame.scroll_to_anchor(anchor);
            } else {
                document->set_active_element(link);
                if (m_frame.is_top_level()) {
                    if (auto* page = m_frame.page())
                        page->client().page_did_click_link(url, link->target(), modifiers);
                } else {
                    m_frame.loader().load(url, FrameLoader::Type::Navigation);
                }
            }
        } else if (button == GUI::MouseButton::Right) {
            if (auto* page = m_frame.page())
                page->client().page_did_request_link_context_menu(m_frame.to_top_level_position(position), url, link->target(), modifiers);
        } else if (button == GUI::MouseButton::Middle) {
            if (auto* page = m_frame.page())
                page->client().page_did_middle_click_link(url, link->target(), modifiers);
        }
    } else {
        if (button == GUI::MouseButton::Left) {
            auto result = layout_root()->hit_test(position, Layout::HitTestType::TextCursor);
            if (result.layout_node && result.layout_node->dom_node()) {
                m_frame.set_cursor_position(DOM::Position(*result.layout_node->dom_node(), result.index_in_node));
                layout_root()->set_selection({ { result.layout_node, result.index_in_node }, {} });
                m_in_mouse_selection = true;
            }
        } else if (button == GUI::MouseButton::Right) {
            if (auto* page = m_frame.page())
                page->client().page_did_request_context_menu(m_frame.to_top_level_position(position));
        }
    }
    return true;
}

bool EventHandler::handle_mousemove(const Gfx::IntPoint& position, unsigned buttons, unsigned modifiers)
{
    if (!layout_root())
        return false;

    if (m_mouse_event_tracking_layout_node) {
        m_mouse_event_tracking_layout_node->handle_mousemove({}, position, buttons, modifiers);
        return true;
    }

    auto& document = *m_frame.document();

    bool hovered_node_changed = false;
    bool is_hovering_link = false;
    Gfx::StandardCursor hovered_node_cursor = Gfx::StandardCursor::None;
    auto result = layout_root()->hit_test(position, Layout::HitTestType::Exact);
    const HTML::HTMLAnchorElement* hovered_link_element = nullptr;
    if (result.layout_node) {

        if (result.layout_node->wants_mouse_events()) {
            document.set_hovered_node(result.layout_node->dom_node());
            result.layout_node->handle_mousemove({}, position, buttons, modifiers);

            if (auto* page = m_frame.page())
                page->client().page_did_request_cursor_change(Gfx::StandardCursor::None);
            return true;
        }

        RefPtr<DOM::Node> node = result.layout_node->dom_node();

        if (node && is<HTML::HTMLIFrameElement>(*node)) {
            if (auto* subframe = verify_cast<HTML::HTMLIFrameElement>(*node).nested_browsing_context())
                return subframe->event_handler().handle_mousemove(position.translated(compute_mouse_event_offset({}, *result.layout_node)), buttons, modifiers);
            return false;
        }

        hovered_node_changed = node != document.hovered_node();
        document.set_hovered_node(node);
        if (node) {
            hovered_link_element = node->enclosing_link_element();
            if (hovered_link_element)
                is_hovering_link = true;

            auto cursor = result.layout_node->computed_values().cursor();
            if (node->is_text() && cursor == CSS::Cursor::Auto)
                hovered_node_cursor = Gfx::StandardCursor::IBeam;
            else
                hovered_node_cursor = cursor_css_to_gfx(cursor);

            auto offset = compute_mouse_event_offset(position, *result.layout_node);
            node->dispatch_event(UIEvents::MouseEvent::create(UIEvents::EventNames::mousemove, offset.x(), offset.y(), position.x(), position.y()));

            if (!layout_root() || layout_root() != node->document().layout_node())
                return true;
        }
        if (m_in_mouse_selection) {
            auto hit = layout_root()->hit_test(position, Layout::HitTestType::TextCursor);
            if (hit.layout_node && hit.layout_node->dom_node()) {
                m_frame.set_cursor_position(DOM::Position(*hit.layout_node->dom_node(), result.index_in_node));
                layout_root()->set_selection_end({ hit.layout_node, hit.index_in_node });
            }
            if (auto* page = m_frame.page())
                page->client().page_did_change_selection();
        }
    }

    if (auto* page = m_frame.page()) {
        page->client().page_did_request_cursor_change(hovered_node_cursor);

        if (hovered_node_changed) {
            RefPtr<HTML::HTMLElement> hovered_html_element = document.hovered_node() ? document.hovered_node()->enclosing_html_element_with_attribute(HTML::AttributeNames::title) : nullptr;
            if (hovered_html_element && !hovered_html_element->title().is_null()) {
                page->client().page_did_enter_tooltip_area(m_frame.to_top_level_position(position), hovered_html_element->title());
            } else {
                page->client().page_did_leave_tooltip_area();
            }
            if (is_hovering_link)
                page->client().page_did_hover_link(document.complete_url(hovered_link_element->href()));
            else
                page->client().page_did_unhover_link();
        }
    }
    return true;
}

bool EventHandler::focus_next_element()
{
    if (!m_frame.document())
        return false;
    auto* element = m_frame.document()->focused_element();
    if (!element) {
        element = m_frame.document()->first_child_of_type<DOM::Element>();
        if (element && element->is_focusable()) {
            m_frame.document()->set_focused_element(element);
            return true;
        }
    }

    for (element = element->next_element_in_pre_order(); element && !element->is_focusable(); element = element->next_element_in_pre_order())
        ;

    m_frame.document()->set_focused_element(element);
    return element;
}

bool EventHandler::focus_previous_element()
{
    return false;
}

constexpr bool should_ignore_keydown_event(u32 code_point)
{
    return code_point == 0;
}

bool EventHandler::handle_keydown(KeyCode key, unsigned modifiers, u32 code_point)
{
    if (key == KeyCode::Key_Tab) {
        if (modifiers & KeyModifier::Mod_Shift)
            return focus_previous_element();
        else
            return focus_next_element();
    }

    if (layout_root()->selection().is_valid()) {
        auto range = layout_root()->selection().to_dom_range()->normalized();
        if (range->start_container()->is_editable()) {
            m_frame.document()->layout_node()->set_selection({});

            m_frame.set_cursor_position({ *range->start_container(), range->start_offset() });

            if (key == KeyCode::Key_Backspace || key == KeyCode::Key_Delete) {
                m_edit_event_handler->handle_delete(range);
                return true;
            } else if (!should_ignore_keydown_event(code_point)) {
                m_edit_event_handler->handle_delete(range);
                m_edit_event_handler->handle_insert(m_frame.cursor_position(), code_point);
                m_frame.increment_cursor_position_offset();
                return true;
            }
        }
    }

    if (m_frame.cursor_position().is_valid() && m_frame.cursor_position().node()->is_editable()) {
        if (key == KeyCode::Key_Backspace) {
            if (!m_frame.decrement_cursor_position_offset()) {
                return true;
            }

            m_edit_event_handler->handle_delete_character_after(m_frame.cursor_position());
            return true;
        } else if (key == KeyCode::Key_Delete) {
            if (m_frame.cursor_position().offset_is_at_end_of_node()) {
                return true;
            }
            m_edit_event_handler->handle_delete_character_after(m_frame.cursor_position());
            return true;
        } else if (key == KeyCode::Key_Right) {
            if (!m_frame.increment_cursor_position_offset()) {
            }
            return true;
        } else if (key == KeyCode::Key_Left) {
            if (!m_frame.decrement_cursor_position_offset()) {
            }
            return true;
        } else if (!should_ignore_keydown_event(code_point)) {
            m_edit_event_handler->handle_insert(m_frame.cursor_position(), code_point);
            m_frame.increment_cursor_position_offset();
            return true;
        } else {
            return true;
        }
    }

    return false;
}

void EventHandler::set_mouse_event_tracking_layout_node(Layout::Node* layout_node)
{
    if (layout_node)
        m_mouse_event_tracking_layout_node = layout_node->make_weak_ptr();
    else
        m_mouse_event_tracking_layout_node = nullptr;
}

}