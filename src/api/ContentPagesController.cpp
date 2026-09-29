/**
 * @file ContentPagesController.cpp
 * @brief Bodies for src/api/ContentPagesController.hpp — compiled once into
 *        app_core. Contract and the module-off degradation rules are
 *        documented on the declarations in the header.
 */

#include "api/ContentPagesController.hpp"

#include <vector>

#include "api/HandlerSupport.hpp"
#include "api/PostsController.hpp"
#include "core/Modules.hpp"
#include "repositories/PostRepository.hpp"
#include "utils/Config.hpp"

namespace Api {

void ContentPagesController::post_markdown(const HttpRequestPtr& req,
                                           std::function<void(const HttpResponsePtr&)>&& callback,
                                           const std::string& slug) {
    if (!Core::content_enabled()) {
        callback(not_found_markdown());
        return;
    }
    with_repo_errors(callback, "post_markdown", [&] {
        auto found = PostsController::resolve_post(slug, req->getParameter("preview"));
        if (!found) {
            callback(not_found_markdown());
            return;
        }
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("# " + found->title + "\n\n" + found->body);
        // Drogon's CT_* enum has no Markdown entry — set the header directly.
        resp->setContentTypeString("text/markdown; charset=utf-8");
        callback(resp);
    });
}

void ContentPagesController::sitemap(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback) {
    (void)req;
    with_repo_errors(callback, "sitemap", [&] {
        std::vector<Repositories::PostRepository::SitemapEntry> entries;
        if (Core::content_enabled()) {
            Repositories::PostRepository repo;
            entries = repo.list_published_for_sitemap();
        }
        // Escaped once: the base URL is loop-invariant, only slugs vary.
        const std::string base = esc(base_url());

        std::string xml;
        xml.reserve(entries.size() * 128 + 256);
        xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        xml += "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";
        xml += "  <url><loc>" + base + "/</loc><changefreq>monthly</changefreq><priority>1.0</priority></url>\n";
        for (const auto& e : entries) {
            xml += "  <url><loc>" + base + "/posts/" + esc(e.slug) + "</loc>";
            if (!e.lastmod.empty())
                xml += "<lastmod>" + e.lastmod + "</lastmod>";
            xml += "<changefreq>monthly</changefreq><priority>0.6</priority></url>\n";
        }
        xml += "</urlset>\n";

        auto resp = HttpResponse::newHttpResponse();
        resp->setBody(std::move(xml));
        resp->setContentTypeString("application/xml; charset=utf-8");
        // Cheap SQL, but crawlers poll: an hour of caching is plenty fresh.
        resp->addHeader("Cache-Control", "public, max-age=3600");
        callback(resp);
    });
}

drogon::HttpResponsePtr ContentPagesController::not_found_markdown() {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k404NotFound);
    resp->setContentTypeString("text/markdown; charset=utf-8");
    resp->setBody("# 404\n\nNot found.\n");
    return resp;
}

std::string ContentPagesController::base_url() {
    std::string base = "http://localhost:8080";
    if (Config::is_initialized())
        base = Config::get().get<std::string>("app.base_url", "APP_BASE_URL", base);
    if (!base.empty() && base.back() == '/')
        base.pop_back();
    return base;
}

std::string ContentPagesController::esc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '&':
                o += "&amp;";
                break;
            case '<':
                o += "&lt;";
                break;
            case '>':
                o += "&gt;";
                break;
            case '"':
                o += "&quot;";
                break;
            case '\'':
                o += "&#39;";
                break;
            default:
                o += c;
        }
    }
    return o;
}

}  // namespace Api
