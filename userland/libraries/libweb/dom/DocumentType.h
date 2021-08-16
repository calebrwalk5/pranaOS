/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <base/FlyString.h>
#include <libweb/dom/Node.h>

namespace Web::DOM {

class DocumentType final : public Node {
public:
    using WrapperType = Bindings::DocumentTypeWrapper;

    static NonnullRefPtr<DocumentType> create(Document& document)
    {
        return adopt_ref(*new DocumentType(document));
    }

    explicit DocumentType(Document&);
    virtual ~DocumentType() override;

    virtual FlyString node_name() const override { return "#doctype"; }

    const String& name() const { return m_name; }
    void set_name(const String& name) { m_name = name; }

    const String& public_id() const { return m_public_id; }
    void set_public_id(const String& public_id) { m_public_id = public_id; }

    const String& system_id() const { return m_system_id; }
    void set_system_id(const String& system_id) { m_system_id = system_id; }

private:
    String m_name;
    String m_public_id;
    String m_system_id;
};

}