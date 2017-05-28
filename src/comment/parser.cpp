// Copyright (C) 2016-2017 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#include <standardese/comment/parser.hpp>

#include <cassert>
#include <cmark.h>

#include <standardese/markup/paragraph.hpp>

using namespace standardese;
using namespace standardese::comment;

parser::parser() : parser_(cmark_parser_new(CMARK_OPT_NORMALIZE | CMARK_OPT_SMART))
{
}

parser::~parser()
{
    cmark_parser_free(parser_);
}

ast_root::~ast_root()
{
    if (root_)
        cmark_node_free(root_);
}

ast_root standardese::comment::read_ast(const parser& p, const std::string& comment)
{
    cmark_parser_feed(p.get(), comment.c_str(), comment.size());
    auto root = cmark_parser_finish(p.get());
    return ast_root(root);
}

namespace
{
    template <class Builder>
    void add_children(Builder& b, cmark_node* parent);

    std::unique_ptr<markup::paragraph> parse_paragraph(cmark_node* node)
    {
        assert(cmark_node_get_type(node) == CMARK_NODE_PARAGRAPH);

        markup::paragraph::builder builder;
        add_children(builder, node);
        return builder.finish();
    }

    std::unique_ptr<markup::text> parse_text(cmark_node* node)
    {
        assert(cmark_node_get_type(node) == CMARK_NODE_TEXT);
        return markup::text::build(cmark_node_get_literal(node));
    }

    std::unique_ptr<markup::text> parse_soft_break(cmark_node* node)
    {
        assert(cmark_node_get_type(node) == CMARK_NODE_SOFTBREAK);
        return markup::text::build(" ");
    }

    template <class Builder, typename T>
    auto add_child(int, Builder& b, std::unique_ptr<T> entity)
        -> decltype(b.add_child(std::move(entity)))
    {
        return b.add_child(std::move(entity));
    }

    template <class Builder, typename T>
    void add_child(short, Builder&, std::unique_ptr<T>)
    {
        assert(!"unexpected child");
    }

    template <class Builder>
    void add_children(Builder& b, cmark_node* parent)
    {
        auto cur  = cmark_node_first_child(parent);
        auto last = cmark_node_last_child(parent);
        while (true)
        {
            switch (cmark_node_get_type(cur))
            {
            case CMARK_NODE_PARAGRAPH:
                add_child(0, b, parse_paragraph(cur));
                break;

            case CMARK_NODE_TEXT:
                add_child(0, b, parse_text(cur));
                break;
            case CMARK_NODE_SOFTBREAK:
                add_child(0, b, parse_soft_break(cur));
                break;

            case CMARK_NODE_NONE:
            case CMARK_NODE_DOCUMENT:
            case CMARK_NODE_BLOCK_QUOTE:
            case CMARK_NODE_LIST:
            case CMARK_NODE_ITEM:
            case CMARK_NODE_CODE_BLOCK:
            case CMARK_NODE_HTML_BLOCK:
            case CMARK_NODE_CUSTOM_BLOCK:
            case CMARK_NODE_HEADING:
            case CMARK_NODE_THEMATIC_BREAK:
            case CMARK_NODE_LINEBREAK:
            case CMARK_NODE_CODE:
            case CMARK_NODE_HTML_INLINE:
            case CMARK_NODE_CUSTOM_INLINE:
            case CMARK_NODE_EMPH:
            case CMARK_NODE_STRONG:
            case CMARK_NODE_LINK:
            case CMARK_NODE_IMAGE:
                assert(!"unhandled node type");
                break;
            }

            if (cur == last)
                break;
            cur = cmark_node_next(cur);
        }
    }
}

translated_ast standardese::comment::translate_ast(const ast_root& root)
{
    assert(cmark_node_get_type(root.get()) == CMARK_NODE_DOCUMENT);
    translated_ast result;

    // just assume details for now
    markup::details_section::builder builder;
    add_children(builder, root.get());
    result.sections.push_back(builder.finish());

    return result;
}
