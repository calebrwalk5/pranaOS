/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// includes
#include <libweb/dom/Event.h>
#include <libweb/dom/ShadowRoot.h>
#include <libweb/layout/BlockBox.h>

namespace Web::DOM {

ShadowRoot::ShadowRoot(Document& document, Element& host)
    : DocumentFragment(document)
{
    set_host(host);
}

EventTarget* ShadowRoot::get_parent(const Event& event)
{
    if (!event.composed()) {
        auto& events_first_invocation_target = verify_cast<Node>(*event.path().first().invocation_target);
        if (events_first_invocation_target.root() == this)
            return nullptr;
    }

    return host();
}

RefPtr<Layout::Node> ShadowRoot::create_layout_node()
{
    return adopt_ref(*new Layout::BlockBox(document(), this, CSS::ComputedValues {}));
}

}