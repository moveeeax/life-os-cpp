/**
 * @file ContentPagesController.hpp
 * @brief Public, unauthenticated content surfaces for the posts module: raw
 *        Markdown delivery at a clean per-post URL and a dynamic sitemap.xml.
 *
 * Slimmed-down counterpart of the fork's PublicPagesController — this
 * template ships no HTML rendering layer (no PageTemplates/Pages::render),
 * so the fork's SSR head/JSON-LD/hydration-island machinery for /blog/{slug}
 * is dropped entirely. What's left is exactly what the fork's own comment on
 * that file called out as a *separate* concern from the SSR shell: the
 * sitemap, and — here, in place of an HTML page — the raw Markdown body a
 * static frontend or another renderer can fetch directly.
 *
 * Declarations only — the handler bodies live in ContentPagesController.cpp
 * (compiled once into app_core; ADR 0003 as amended 2026-08-22). The route
 * macros (ADD_METHOD_TO) must stay in this header: Drogon's METHOD_LIST
 * registration is part of the class definition, and
 * scripts/check-routes-registered.sh greps the src/api headers for them.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class ContentPagesController : public HttpController<ContentPagesController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ContentPagesController::post_markdown, "/posts/{1}", Get);
    ADD_METHOD_TO(ContentPagesController::sitemap, "/sitemap.xml", Get);
    METHOD_LIST_END

    // GET /posts/{slug} — the post's raw Markdown body, prefixed by a single
    // "# {title}" heading line. Published posts by default; an optional
    // ?preview=<token> reveals a DRAFT when the token verifies AND is bound
    // to that post — delegates the published-or-preview resolution to
    // PostsController::resolve_post, the same helper the admin-issued
    // preview link (PostsController::previewToken) and the JSON
    // publicGetPost route share, so all three surfaces agree on what a
    // preview token unlocks. Module-off, unknown slug, drafts without a
    // (valid, matching) token, and expired/foreign tokens all collapse to
    // the same 404 Markdown body so none of them leak which case applied.
    void post_markdown(const HttpRequestPtr& req,
                       std::function<void(const HttpResponsePtr&)>&& callback,
                       const std::string& slug);

    // GET /sitemap.xml — home + one <url> per published post (clean
    // /posts/<slug> URL). Ported from the fork's sitemap loop verbatim minus
    // the /blog.html index entry, which has no equivalent page in this
    // template (no HTML rendering layer is ported — see file docstring).
    //
    // Guarded the same as post_markdown, but a disabled/not-yet-migrated
    // module degrades to a root-only sitemap instead of a 404: a sitemap
    // conceptually always exists for the site (see PostsController.hpp's
    // docstring on why the guard exists — an un-migrated deploy has no
    // `posts` table yet, so skipping the repository call here, not just the
    // response, is what avoids a 500).
    void sitemap(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

private:
    static drogon::HttpResponsePtr not_found_markdown();

    // Canonical origin for absolute sitemap URLs. Same lookup/trim as
    // src/email/AccountEmails.hpp's base_url() — app.base_url is this repo's
    // one site-origin config key (the fork's PublicPagesController instead
    // reads a "site.base_url" key that doesn't exist here, with a
    // header-derived dev fallback; that machinery isn't ported — a sitemap
    // has no request-scoped notion of "origin", it's a single canonical URL
    // set, so the configured base is authoritative and nothing else applies).
    static std::string base_url();

    // XML-escape of the five significant characters. No shared helper for
    // this exists in Utils::Strings (Task 2 confirmed Post.hpp/
    // PostRepository.hpp reference none, and left the file untouched); the
    // fork itself doesn't use one either — PublicPagesController.hpp carries
    // this exact same escaper as a private method. Ported verbatim.
    static std::string esc(const std::string& s);
};

}  // namespace Api
