/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// includes
#include <libweb/html/HTMLMetaElement.h>

namespace Web::HTML {

HTMLMetaElement::HTMLMetaElement(DOM::Document& document, QualifiedName qualified_name)
    : HTMLElement(document, move(qualified_name))
{
}

HTMLMetaElement::~HTMLMetaElement()
{
}

}