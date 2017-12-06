// Copyright (C) 2016-2017 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#include <standardese/markup/generator.hpp>

#include <cassert>
#include <cmark.h>
#include <ostream>

#include <standardese/markup/block.hpp>
#include <standardese/markup/code_block.hpp>
#include <standardese/markup/doc_section.hpp>
#include <standardese/markup/document.hpp>
#include <standardese/markup/documentation.hpp>
#include <standardese/markup/entity.hpp>
#include <standardese/markup/entity_kind.hpp>
#include <standardese/markup/heading.hpp>
#include <standardese/markup/index.hpp>
#include <standardese/markup/link.hpp>
#include <standardese/markup/list.hpp>
#include <standardese/markup/paragraph.hpp>
#include <standardese/markup/phrasing.hpp>
#include <standardese/markup/quote.hpp>
#include <standardese/markup/thematic_break.hpp>

using namespace standardese::markup;

namespace
{
    void build_entity(cmark_node* parent, bool use_html, const entity& e);

    template <typename T>
    void handle_children(cmark_node* node, bool use_html, const T& container)
    {
        for (auto& child : container)
            build_entity(node, use_html, child);
    }

    cmark_node* build_emph(const char* str)
    {
        auto emph = cmark_node_new(CMARK_NODE_EMPH);
        auto text = cmark_node_new(CMARK_NODE_TEXT);
        cmark_node_set_literal(text, str);
        cmark_node_append_child(emph, text);
        return emph;
    }

    cmark_node* build_heading(unsigned level, const char* str)
    {
        auto heading = cmark_node_new(CMARK_NODE_HEADING);
        cmark_node_set_heading_level(heading, level);

        if (str)
        {
            auto text = cmark_node_new(CMARK_NODE_TEXT);
            cmark_node_set_literal(text, str);
            cmark_node_append_child(heading, text);
        }

        return heading;
    }

    void build(cmark_node* parent, bool use_html, const code_block& cb);

    void build_list_item(cmark_node* list, bool use_html, const list_item_base& item);

    void build_documentation(cmark_node* parent, bool use_html, const documentation_entity& doc)
    {
        if (use_html)
        {
            auto html = cmark_node_new(CMARK_NODE_HTML_BLOCK);
            cmark_node_set_literal(html,
                                   ("<a id=\"standardese-" + doc.id().as_str() + "\"/>").c_str());
            cmark_node_append_child(parent, html);
        }

        if (doc.synopsis())
            build(parent, use_html, doc.synopsis().value());

        if (auto brief = doc.brief_section())
        {
            auto paragraph = cmark_node_new(CMARK_NODE_PARAGRAPH);
            handle_children(paragraph, use_html, brief.value());
            cmark_node_append_child(parent, paragraph);
        }

        // write inline sections
        {
            for (auto& section : doc.doc_sections())
                if (section.kind() != entity_kind::inline_section)
                    continue;
                else
                {
                    auto& sec = static_cast<const inline_section&>(section);

                    auto paragraph = cmark_node_new(CMARK_NODE_PARAGRAPH);
                    cmark_node_append_child(parent, paragraph);

                    // add section name
                    auto emph = build_emph((sec.name() + ": ").c_str());
                    cmark_node_append_child(paragraph, emph);

                    // build section content
                    handle_children(paragraph, use_html, sec);
                }
        }

        // write details section
        if (auto details = doc.details_section())
            handle_children(parent, use_html, details.value());

        // write list sections
        for (auto& section : doc.doc_sections())
            if (section.kind() != entity_kind::list_section)
                continue;
            else
            {
                auto& list = static_cast<const list_section&>(section);

                // heading
                auto heading = build_heading(4, list.name().c_str());
                cmark_node_append_child(parent, heading);

                // list
                auto ul = cmark_node_new(CMARK_NODE_LIST);
                cmark_node_set_list_type(ul, CMARK_BULLET_LIST);
                cmark_node_set_list_tight(ul, 1);
                cmark_node_append_child(parent, ul);

                for (auto& item : list)
                    build_list_item(ul, use_html, item);
            }
    }

    void append_module(cmark_node* heading, const std::string& module)
    {
        auto text = cmark_node_new(CMARK_NODE_TEXT);
        cmark_node_set_literal(text, (" [" + module + "]").c_str());
        cmark_node_append_child(heading, text);
    }

    void build_doc_header(cmark_node* parent, bool use_html, const documentation_header& header,
                          unsigned level)
    {
        auto heading = build_heading(level, nullptr);
        cmark_node_append_child(parent, heading);

        handle_children(heading, use_html, header.heading());

        if (header.module())
            append_module(heading, header.module().value());
    }

    void build_doc_header(cmark_node* parent, bool use_html, const documentation_entity& doc,
                          unsigned level)
    {
        if (doc.header())
            build_doc_header(parent, use_html, doc.header().value(), level);
    }

    void build(cmark_node* parent, bool use_html, const file_documentation& doc)
    {
        build_doc_header(parent, use_html, doc, 1);
        build_documentation(parent, use_html, doc);
        handle_children(parent, use_html, doc);
    }

    unsigned get_documentation_heading_level(const documentation_entity& doc)
    {
        for (auto cur = doc.parent(); cur; cur = cur.value().parent())
            if (cur.value().kind() == entity_kind::entity_documentation
                || cur.value().kind() == entity_kind::namespace_documentation)
                // use h3 when entity has a parent entity
                return 3;
        // return h2 otherwise
        return 2;
    }

    void build(cmark_node* parent, bool use_html, const entity_documentation& doc)
    {
        build_doc_header(parent, use_html, doc, get_documentation_heading_level(doc));
        build_documentation(parent, use_html, doc);
        handle_children(parent, use_html, doc);

        if (doc.header())
            cmark_node_append_child(parent, cmark_node_new(CMARK_NODE_THEMATIC_BREAK));
    }

    void build(cmark_node* parent, bool use_html, const entity_index_item& item);
    void build(cmark_node* parent, bool use_html, const namespace_documentation& doc);
    void build(cmark_node* parent, bool use_html, const module_documentation& doc);

    void build_index_child(cmark_node* parent, bool use_html, const block_entity& child)
    {
        if (child.kind() == entity_kind::entity_index_item)
            build(parent, use_html, static_cast<const entity_index_item&>(child));
        else if (child.kind() == entity_kind::namespace_documentation)
            build(parent, use_html, static_cast<const namespace_documentation&>(child));
        else if (child.kind() == entity_kind::module_documentation)
            build(parent, use_html, static_cast<const module_documentation&>(child));
        else
            assert(false);
    }

    template <class T>
    void build_module_ns(cmark_node* parent, bool use_html, const T& doc)
    {
        auto item = cmark_node_new(CMARK_NODE_ITEM);
        cmark_node_append_child(parent, item);

        build_doc_header(item, use_html, doc, get_documentation_heading_level(doc));
        build_documentation(item, use_html, doc);

        auto list = cmark_node_new(CMARK_NODE_LIST);
        cmark_node_set_list_type(list, CMARK_BULLET_LIST);
        cmark_node_append_child(item, list);

        for (auto& child : doc)
            build_index_child(list, use_html, child);
    }

    void build(cmark_node* parent, bool use_html, const namespace_documentation& doc)
    {
        build_module_ns(parent, use_html, doc);
    }

    void build(cmark_node* parent, bool use_html, const module_documentation& doc)
    {
        build_module_ns(parent, use_html, doc);
    }

    void build_term_description(cmark_node* parent, bool use_html, const term& t,
                                const description* desc);

    void build(cmark_node* parent, bool use_html, const entity_index_item& item)
    {
        auto node = cmark_node_new(CMARK_NODE_ITEM);
        cmark_node_append_child(parent, node);
        build_term_description(node, use_html, item.entity(),
                               item.brief() ? &item.brief().value() : nullptr);
    }

    template <class Index>
    void build_index(cmark_node* parent, bool use_html, const Index& index)
    {
        auto heading = build_heading(1, nullptr);
        cmark_node_append_child(parent, heading);
        handle_children(heading, use_html, index.heading());

        auto list = cmark_node_new(CMARK_NODE_LIST);
        cmark_node_set_list_type(list, CMARK_BULLET_LIST);
        cmark_node_append_child(parent, list);

        for (auto& child : index)
            build_index_child(list, use_html, child);
    }

    void build(cmark_node* parent, bool use_html, const file_index& index)
    {
        build_index(parent, use_html, index);
    }

    void build(cmark_node* parent, bool use_html, const entity_index& index)
    {
        build_index(parent, use_html, index);
    }

    void build(cmark_node* parent, bool use_html, const module_index& index)
    {
        build_index(parent, use_html, index);
    }

    void build(cmark_node* parent, bool use_html, const heading& h)
    {
        auto heading = build_heading(4, nullptr);
        cmark_node_append_child(parent, heading);
        handle_children(heading, use_html, h);
    }

    void build(cmark_node* parent, bool use_html, const subheading& h)
    {
        auto heading = build_heading(5, nullptr);
        cmark_node_append_child(parent, heading);
        handle_children(heading, use_html, h);
    }

    void build(cmark_node* parent, bool use_html, const paragraph& par)
    {
        auto node = cmark_node_new(CMARK_NODE_PARAGRAPH);
        cmark_node_append_child(parent, node);
        handle_children(node, use_html, par);
    }

    void build_term_description(cmark_node* parent, bool use_html, const term& t,
                                const description* desc)
    {
        auto paragraph = cmark_node_new(CMARK_NODE_PARAGRAPH);
        cmark_node_append_child(parent, paragraph);

        handle_children(paragraph, use_html, t);

        if (desc)
        {
            if (use_html)
            {
                auto html = cmark_node_new(CMARK_NODE_HTML_INLINE);
                cmark_node_set_literal(html, " &mdash; ");
                cmark_node_append_child(paragraph, html);
            }
            else
            {
                auto text = cmark_node_new(CMARK_NODE_TEXT);
                cmark_node_set_literal(text, " - ");
                cmark_node_append_child(paragraph, text);
            }

            handle_children(paragraph, use_html, *desc);
        }
    }

    void build_list_item(cmark_node* parent, bool use_html, const list_item_base& item)
    {
        auto li = cmark_node_new(CMARK_NODE_ITEM);
        cmark_node_append_child(parent, li);

        if (item.kind() == entity_kind::list_item)
            handle_children(li, use_html, static_cast<const list_item&>(item));
        else if (item.kind() == entity_kind::term_description_item)
        {
            auto& term        = static_cast<const term_description_item&>(item).term();
            auto& description = static_cast<const term_description_item&>(item).description();
            build_term_description(li, parent, term, &description);
        }
        else
            assert(false);
    }

    void build(cmark_node* parent, bool use_html, const unordered_list& list)
    {
        auto ul = cmark_node_new(CMARK_NODE_LIST);
        cmark_node_set_list_type(ul, CMARK_BULLET_LIST);
        cmark_node_append_child(parent, ul);

        for (auto& item : list)
            build_list_item(ul, use_html, item);
    }

    void build(cmark_node* parent, bool use_html, const ordered_list& list)
    {
        auto ul = cmark_node_new(CMARK_NODE_LIST);
        cmark_node_set_list_type(ul, CMARK_ORDERED_LIST);
        cmark_node_set_list_start(ul, 1);
        cmark_node_append_child(parent, ul);

        for (auto& item : list)
            build_list_item(ul, use_html, item);
    }

    void build(cmark_node* parent, bool use_html, const block_quote& quote)
    {
        auto node = cmark_node_new(CMARK_NODE_BLOCK_QUOTE);
        cmark_node_append_child(parent, node);

        handle_children(node, use_html, quote);
    }

    void build(cmark_node* parent, bool use_html, const code_block& cb)
    {
        if (use_html)
        {
            auto node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
            cmark_node_append_child(parent, node);

            auto html = render(html_generator("md"), cb);
            cmark_node_set_literal(node, html.c_str());
        }
        else
        {
            auto node = cmark_node_new(CMARK_NODE_CODE_BLOCK);
            cmark_node_append_child(parent, node);

            if (!cb.language().empty())
                cmark_node_set_fence_info(node, cb.language().c_str());

            handle_children(node, use_html, cb);
        }
    }

    void append_code_block_text(cmark_node* cb, const std::string& text)
    {
        auto str = cmark_node_get_literal(cb);
        if (str)
            cmark_node_set_literal(cb, (str + text).c_str());
        else
            cmark_node_set_literal(cb, text.c_str());
    }

    void build(cmark_node* parent, bool, const code_block::keyword& text)
    {
        append_code_block_text(parent, text.string());
    }

    void build(cmark_node* parent, bool, const code_block::identifier& text)
    {
        append_code_block_text(parent, text.string());
    }

    void build(cmark_node* parent, bool, const code_block::string_literal& text)
    {
        append_code_block_text(parent, text.string());
    }

    void build(cmark_node* parent, bool, const code_block::int_literal& text)
    {
        append_code_block_text(parent, text.string());
    }

    void build(cmark_node* parent, bool, const code_block::float_literal& text)
    {
        append_code_block_text(parent, text.string());
    }

    void build(cmark_node* parent, bool, const code_block::punctuation& text)
    {
        append_code_block_text(parent, text.string());
    }

    void build(cmark_node* parent, bool, const code_block::preprocessor& text)
    {
        append_code_block_text(parent, text.string());
    }

    void build(cmark_node* parent, bool, const thematic_break&)
    {
        auto node = cmark_node_new(CMARK_NODE_THEMATIC_BREAK);
        cmark_node_append_child(parent, node);
    }

    void build(cmark_node* parent, bool, const text& t)
    {
        if (cmark_node_get_type(parent) == CMARK_NODE_CODE_BLOCK
            || cmark_node_get_type(parent) == CMARK_NODE_CODE)
            append_code_block_text(parent, t.string());
        else
        {
            auto text = cmark_node_new(CMARK_NODE_TEXT);
            cmark_node_append_child(parent, text);
            cmark_node_set_literal(text, t.string().c_str());
        }
    }

    void build(cmark_node* parent, bool use_html, const emphasis& emph)
    {
        auto node = cmark_node_new(CMARK_NODE_EMPH);
        cmark_node_append_child(parent, node);

        handle_children(node, use_html, emph);
    }

    void build(cmark_node* parent, bool use_html, const strong_emphasis& emph)
    {
        auto node = cmark_node_new(CMARK_NODE_STRONG);
        cmark_node_append_child(parent, node);

        handle_children(node, use_html, emph);
    }

    void build(cmark_node* parent, bool use_html, const code& c)
    {
        auto node = cmark_node_new(CMARK_NODE_CODE);
        cmark_node_append_child(parent, node);
        handle_children(node, use_html, c);
    }

    void build(cmark_node* parent, bool, const soft_break&)
    {
        auto node = cmark_node_new(CMARK_NODE_SOFTBREAK);
        cmark_node_append_child(parent, node);
    }

    void build(cmark_node* parent, bool, const hard_break&)
    {
        auto node = cmark_node_new(CMARK_NODE_LINEBREAK);
        cmark_node_append_child(parent, node);
    }

    cmark_node* build_link(const char* title, const char* url)
    {
        auto node = cmark_node_new(CMARK_NODE_LINK);
        if (*title != '\0')
            cmark_node_set_title(node, title);
        cmark_node_set_url(node, url);

        return node;
    }

    void build(cmark_node* parent, bool use_html, const external_link& link)
    {
        if (cmark_node_get_type(parent) == CMARK_NODE_CODE_BLOCK)
            handle_children(parent, use_html, link);
        else
        {
            auto node = build_link(link.title().c_str(), link.url().as_str().c_str());
            cmark_node_append_child(parent, node);

            handle_children(node, use_html, link);
        }
    }

    void build(cmark_node* parent, bool use_html, const documentation_link& link)
    {
        if (cmark_node_get_type(parent) == CMARK_NODE_CODE_BLOCK)
            handle_children(parent, use_html, link);
        else if (link.internal_destination())
        {
            auto url = link.internal_destination()
                           .value()
                           .document()
                           .map(&output_name::file_name, "md")
                           .value_or("");
            url += "#standardese-" + link.internal_destination().value().id().as_str();

            auto node = build_link(link.title().c_str(), url.c_str());
            cmark_node_append_child(parent, node);

            handle_children(node, use_html, link);
        }
        else if (link.external_destination())
        {
            auto url = link.external_destination().value().as_str();

            auto node = build_link(link.title().c_str(), url.c_str());
            cmark_node_append_child(parent, node);

            handle_children(node, use_html, link);
        }
        else
            // only write link content
            handle_children(parent, use_html, link);
    }

    void build_entity(cmark_node* parent, bool use_html, const entity& e)
    {
        switch (e.kind())
        {
#define STANDARDESE_DETAIL_HANDLE(Kind)                                                            \
    case entity_kind::Kind:                                                                        \
        build(parent, use_html, static_cast<const Kind&>(e));                                      \
        break;
#define STANDARDESE_DETAIL_HANDLE_CODE_BLOCK(Kind)                                                 \
    case entity_kind::code_block_##Kind:                                                           \
        build(parent, use_html, static_cast<const code_block::Kind&>(e));                          \
        break;

            STANDARDESE_DETAIL_HANDLE(file_documentation)
            STANDARDESE_DETAIL_HANDLE(entity_documentation)
            STANDARDESE_DETAIL_HANDLE(module_documentation)

            STANDARDESE_DETAIL_HANDLE(file_index)
            STANDARDESE_DETAIL_HANDLE(entity_index)
            STANDARDESE_DETAIL_HANDLE(module_index)

            STANDARDESE_DETAIL_HANDLE(heading)
            STANDARDESE_DETAIL_HANDLE(subheading)

            STANDARDESE_DETAIL_HANDLE(paragraph)

            STANDARDESE_DETAIL_HANDLE(unordered_list)
            STANDARDESE_DETAIL_HANDLE(ordered_list)

            STANDARDESE_DETAIL_HANDLE(block_quote)

            STANDARDESE_DETAIL_HANDLE(code_block)
            STANDARDESE_DETAIL_HANDLE_CODE_BLOCK(keyword)
            STANDARDESE_DETAIL_HANDLE_CODE_BLOCK(identifier)
            STANDARDESE_DETAIL_HANDLE_CODE_BLOCK(string_literal)
            STANDARDESE_DETAIL_HANDLE_CODE_BLOCK(int_literal)
            STANDARDESE_DETAIL_HANDLE_CODE_BLOCK(float_literal)
            STANDARDESE_DETAIL_HANDLE_CODE_BLOCK(punctuation)
            STANDARDESE_DETAIL_HANDLE_CODE_BLOCK(preprocessor)

            STANDARDESE_DETAIL_HANDLE(thematic_break)

            STANDARDESE_DETAIL_HANDLE(text)
            STANDARDESE_DETAIL_HANDLE(emphasis)
            STANDARDESE_DETAIL_HANDLE(strong_emphasis)
            STANDARDESE_DETAIL_HANDLE(code)
            STANDARDESE_DETAIL_HANDLE(soft_break)
            STANDARDESE_DETAIL_HANDLE(hard_break)

            STANDARDESE_DETAIL_HANDLE(external_link)
            STANDARDESE_DETAIL_HANDLE(documentation_link)

#undef STANDARDESE_DETAIL_HANDLE
#undef STANDARDESE_DETAIL_HANDLE_CODE_BLOCK

        case entity_kind::main_document:
        case entity_kind::subdocument:
        case entity_kind::template_document:
        case entity_kind::namespace_documentation:
        case entity_kind::entity_index_item:
        case entity_kind::list_item:
        case entity_kind::term:
        case entity_kind::description:
        case entity_kind::term_description_item:
        case entity_kind::brief_section:
        case entity_kind::details_section:
        case entity_kind::inline_section:
        case entity_kind::list_section:
            assert(!static_cast<bool>("can't use this entity stand-alone"));
            break;
        }
    }
}

generator standardese::markup::markdown_generator(bool use_html) noexcept
{
    return [use_html](std::ostream& out, const entity& e) {
        auto doc = is_phrasing(e.kind()) ? cmark_node_new(CMARK_NODE_PARAGRAPH) :
                                           cmark_node_new(CMARK_NODE_DOCUMENT);

        if (e.kind() == entity_kind::main_document || e.kind() == entity_kind::subdocument
            || e.kind() == entity_kind::template_document)
            handle_children(doc, use_html, static_cast<const document_entity&>(e));
        else
            build_entity(doc, use_html, e);

        auto str = cmark_render_commonmark(doc, CMARK_OPT_NOBREAKS, 0);
        out << str;
        std::free(str);

        cmark_node_free(doc);
    };
}
