/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <base/Optional.h>
#include <base/String.h>
#include <base/StringView.h>
#include <base/Types.h>
#include <base/Utf8View.h>
#include <libweb/css/parser/Token.h>
#include <libweb/Forward.h>

namespace Web::CSS {

class U32Twin {
public:
    void set(size_t index, u32 value)
    {
        if (index == 0)
            first = value;
        if (index == 1)
            second = value;
    }

    u32 first {};
    u32 second {};
};

class U32Triplet {
public:
    void set(size_t index, u32 value)
    {
        if (index == 0)
            first = value;
        if (index == 1)
            second = value;
        if (index == 2)
            third = value;
    }

    U32Twin to_twin_12()
    {
        return { first, second };
    }

    U32Twin to_twin_23()
    {
        return { second, third };
    }

    u32 first {};
    u32 second {};
    u32 third {};
};

class CSSNumber {
public:
    String value;
    Token::NumberType type {};
};

class Tokenizer {

public:
    explicit Tokenizer(const StringView& input, const String& encoding);

    [[nodiscard]] Vector<Token> parse();

    [[nodiscard]] static Token create_eof_token();

private:
    [[nodiscard]] u32 next_code_point();
    [[nodiscard]] u32 peek_code_point(size_t offset = 0) const;
    [[nodiscard]] U32Twin peek_twin() const;
    [[nodiscard]] U32Triplet peek_triplet() const;

    [[nodiscard]] static Token create_new_token(Token::Type);
    [[nodiscard]] static Token create_value_token(Token::Type, String value);
    [[nodiscard]] static Token create_value_token(Token::Type, u32 value);
    [[nodiscard]] Token consume_a_token();
    [[nodiscard]] Token consume_string_token(u32 ending_code_point);
    [[nodiscard]] Token consume_a_numeric_token();
    [[nodiscard]] Token consume_an_ident_like_token();
    [[nodiscard]] CSSNumber consume_a_number();
    [[nodiscard]] String consume_a_name();
    [[nodiscard]] u32 consume_escaped_code_point();
    [[nodiscard]] Token consume_a_url_token();
    void consume_the_remnants_of_a_bad_url();
    void consume_comments();
    void reconsume_current_input_code_point();
    [[nodiscard]] static bool is_valid_escape_sequence(U32Twin);
    [[nodiscard]] bool would_start_an_identifier();
    [[nodiscard]] bool would_start_an_identifier(U32Triplet);
    [[nodiscard]] bool starts_with_a_number() const;
    [[nodiscard]] static bool starts_with_a_number(U32Triplet);

    String m_decoded_input;
    Utf8View m_utf8_view;
    Base::Utf8CodePointIterator m_utf8_iterator;
    Base::Utf8CodePointIterator m_prev_utf8_iterator;
};
}