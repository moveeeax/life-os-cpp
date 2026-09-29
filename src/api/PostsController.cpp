/**
 * @file PostsController.cpp
 * @brief Bodies for src/api/PostsController.hpp — compiled once into
 *        app_core. The module gate, validation rules and preview-token
 *        contract are documented on the declarations in the header.
 */

#include "api/PostsController.hpp"

#include <cstddef>
#include <utility>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "security/Auth.hpp"
#include "security/Tokens.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Time.hpp"

namespace Api {

void PostsController::listPosts(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    if (!require_content_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    const auto page = parse_page_params(req, /*default_limit=*/50, /*max_limit=*/200);
    Repositories::AdminListFilter f;
    f.q = req->getParameter("q");
    f.status = req->getParameter("status");
    f.topic = req->getParameter("topic");
    f.tag = req->getParameter("tag");
    if (!f.status.empty() && f.status != "draft" && f.status != "published") {
        callback(ErrorResponse::bad_request("invalid_status", "status must be draft or published"));
        return;
    }
    with_repo_errors(callback, "listPosts", [&] {
        Repositories::PostRepository repo;
        auto items = repo.list_admin(f, page.limit, page.offset);
        long total = repo.count_admin(f);
        json data = items;
        callback(Response::paginated(data, total, page.limit, page.offset));
    });
}

void PostsController::createPost(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    if (!require_content_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    json body;
    if (!Validation::parse_body(req, body, callback))
        return;
    Repositories::PostInput in;
    if (!read_input(body, in, callback))
        return;
    with_repo_errors(callback, "createPost", [&] {
        Repositories::PostRepository repo;
        auto created = repo.create(in);
        callback(Response::created({{"data", json(created)}}));
    });
}

void PostsController::getPost(const HttpRequestPtr& req,
                              std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) {
    if (!require_content_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    if (!require_valid_uuid(id, callback))
        return;
    with_repo_errors(callback, "getPost", [&] {
        Repositories::PostRepository repo;
        auto found = repo.find(id);
        if (!found) {
            callback(ErrorResponse::not_found("post"));
            return;
        }
        callback(Response::ok({{"data", json(*found)}}));
    });
}

void PostsController::updatePost(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) {
    if (!require_content_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    if (!require_valid_uuid(id, callback))
        return;
    json body;
    if (!Validation::parse_body(req, body, callback))
        return;
    Repositories::PostRepository repo;
    // Read the merge base from the PRIMARY: this is a read-modify-write, so
    // a lagging replica would make a partial PATCH silently revert whatever
    // the omitted fields were last set to. Same reason as
    // AdminController::updateUser's post-write re-read.
    auto existing = repo.find(id, /*from_primary=*/true);
    if (!existing) {
        callback(ErrorResponse::not_found("post"));
        return;
    }
    // PATCH is a partial update: merge the body over the existing post so
    // omitted fields (incl. status → published_at) are preserved, not wiped.
    Repositories::PostInput in;
    if (!merge_input(body, *existing, in, callback))
        return;
    with_repo_errors(callback, "updatePost", [&] {
        auto updated = repo.update(id, in);
        callback(Response::ok({{"data", json(updated)}}));
    });
}

void PostsController::deletePost(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) {
    if (!require_content_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    if (!require_valid_uuid(id, callback))
        return;
    with_repo_errors(callback, "deletePost", [&] {
        Repositories::PostRepository repo;
        repo.remove(id);
        callback(Response::ok({{"message", "Post deleted"}}));
    });
}

void PostsController::previewToken(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) {
    if (!require_content_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    if (!require_valid_uuid(id, callback))
        return;
    with_repo_errors(callback, "previewToken", [&] {
        Repositories::PostRepository repo;
        auto found = repo.find(id);
        if (!found) {
            callback(ErrorResponse::not_found("post"));
            return;
        }
        const auto token = Security::Tokens::issue(
            Security::Auth::get().config().jwt_secret, id, Security::Tokens::Purpose::Preview, kPreviewTtl);
        const auto exp = Utils::Time::epoch_to_iso8601(Utils::Time::now_epoch_seconds() + kPreviewTtl.count());
        callback(
            Response::ok({{"data", {{"url", "/posts/" + found->slug + "?preview=" + token}, {"expires_at", exp}}}}));
    });
}

std::optional<Domain::Post> PostsController::resolve_post(const std::string& slug, const std::string& preview) {
    Repositories::PostRepository repo;
    if (preview.empty())
        return repo.find_published_by_slug(slug);
    auto any = repo.find_by_slug_any(slug);
    if (!any)
        return std::nullopt;
    if (any->status == "published")
        return any;
    auto vr = Security::Tokens::verify(
        Security::Auth::get().config().jwt_secret, preview, Security::Tokens::Purpose::Preview);
    if (vr.ok && vr.sub == any->id)
        return any;
    return std::nullopt;  // invalid/expired/foreign token behaves like 404
}

void PostsController::publicListPosts(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback) {
    if (!require_content_enabled(callback))
        return;
    // Hybrid contract: server-side filters + 1-based paging (?page=);
    // facets embedded on demand so the index needs exactly one request per
    // interaction. limit is hard-clamped to 50 — the old fetch-the-whole-
    // feed ?limit=1000 pattern is gone. The RESPONSE envelope is the
    // template's standard paginated list contract ({data, total, limit,
    // offset} — see Response::paginated / ErrorResponse.hpp), not a
    // bespoke shape: offset is the 0-based equivalent of the 1-based page
    // query param, derived once and reused for both the repo call and the
    // response body so they can't disagree.
    const int limit = clamp_int(req->getParameter("limit"), 10, 1, 50);
    const int page = clamp_int(req->getParameter("page"), 1, 1, 1000000);
    const int offset = (page - 1) * limit;
    Repositories::PublicListFilter f;
    f.topic = req->getParameter("topic");
    f.tag = req->getParameter("tag");
    f.q = req->getParameter("q");

    with_repo_errors(callback, "publicListPosts", [&] {
        Repositories::PostRepository repo;
        auto items = repo.list_published_cards(f, limit, offset);
        long total = repo.count_published(f);
        json data = items;
        json out = {{"data", data}, {"total", total}, {"limit", limit}, {"offset", offset}};

        if (req->getParameter("include").find("facets") != std::string::npos) {
            auto [topics, tags] = repo.facets(f);
            json jt = json::array(), jg = json::array();
            for (const auto& t : topics)
                jt.push_back({{"name", t.name}, {"count", t.count}});
            for (const auto& t : tags)
                jg.push_back({{"name", t.name}, {"count", t.count}});
            out["facets"] = {{"topics", jt}, {"tags", jg}};
        }
        callback(Response::ok(out));
    });
}

void PostsController::publicGetPost(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& slug) {
    if (!require_content_enabled(callback))
        return;
    with_repo_errors(callback, "publicGetPost", [&] {
        auto found = resolve_post(slug, req->getParameter("preview"));
        if (!found) {
            callback(ErrorResponse::not_found("post"));
            return;
        }
        json data = json(*found);
        if (req->getParameter("include").find("adjacent") != std::string::npos) {
            Repositories::PostRepository repo;
            auto [prev, next] = repo.find_adjacent(found->id);
            data["adjacent"] = {{"prev", prev ? json{{"slug", prev->slug}, {"title", prev->title}} : json(nullptr)},
                                {"next", next ? json{{"slug", next->slug}, {"title", next->title}} : json(nullptr)}};
        }
        callback(Response::ok({{"data", data}}));
    });
}

std::vector<std::string> PostsController::validate_tags(const json& body, Validation::Errors& errs) {
    std::vector<std::string> tags;
    if (!body["tags"].is_array()) {
        errs.add("tags", "not_array", "must be an array of strings");
        return tags;
    }
    for (const auto& t : body["tags"]) {
        if (!t.is_string()) {
            errs.add("tags", "not_string", "each tag must be a string");
            break;
        }
        std::string s = t.get<std::string>();
        std::size_t b = s.find_first_not_of(" \t");
        std::size_t e = s.find_last_not_of(" \t");
        if (b == std::string::npos)
            continue;  // blank tag → skip silently
        s = s.substr(b, e - b + 1);
        if (s.size() > 40) {
            errs.add("tags", "too_long", "each tag max length 40");
            break;
        }
        if (s.find(',') != std::string::npos || s.find('\n') != std::string::npos ||
            s.find('\r') != std::string::npos) {
            errs.add("tags", "invalid", "a tag must not contain commas or line breaks");
            break;
        }
        tags.push_back(std::move(s));
    }
    return tags;
}

void PostsController::validate_present_fields(const json& body, Validation::Errors& errs) {
    if (body.contains("slug"))
        Validation::string_length(errs, body, "slug", 1, 160);
    if (body.contains("title"))
        Validation::string_length(errs, body, "title", 1, 255);
    if (body.contains("topic"))
        Validation::string_length(errs, body, "topic", 0, 80);
    if (body.contains("status"))
        Validation::one_of(errs, body, "status", {"draft", "published"});
    // Optional string fields must be strings when present — body.value(k,"")
    // throws type_error.306 on a non-string, escaping the handler as a 500.
    for (const char* k : {"summary", "body", "status", "topic", "title"})
        if (body.contains(k) && !body[k].is_null() && !body[k].is_string())
            errs.add(k, "not_string", std::string(k) + " must be a string");
    // Slug is the public URL key: a clean path segment only.
    if (body.contains("slug") && body["slug"].is_string()) {
        const std::string s = body["slug"].get<std::string>();
        bool ok = !s.empty();
        for (char c : s)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
                ok = false;
        if (!ok || s.front() == '-' || s.back() == '-')
            errs.add(
                "slug", "invalid", "slug must be lowercase letters, digits and hyphens (no leading/trailing hyphen)");
    }
}

bool PostsController::read_input(const json& body,
                                 Repositories::PostInput& in,
                                 const std::function<void(const HttpResponsePtr&)>& callback) {
    Validation::Errors errs;
    Validation::require(errs, body, "slug");
    Validation::require(errs, body, "title");
    validate_present_fields(body, errs);
    std::vector<std::string> tags;
    if (body.contains("tags") && !body["tags"].is_null())
        tags = validate_tags(body, errs);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return false;
    }
    // Null-safe reads: validate_present_fields lets an explicit null through
    // (absent and null both mean "take the default"), but body.value(k, d)
    // throws type_error.302 on a null — a 500 for a client that serializes
    // empty optionals as null. Same pattern merge_input uses.
    in.slug = body["slug"].get<std::string>();
    in.title = body["title"].get<std::string>();
    in.summary = Validation::opt_string(body, "summary").value_or(std::string{});
    in.body = Validation::opt_string(body, "body").value_or(std::string{});
    in.status = Validation::opt_string(body, "status").value_or(std::string{"draft"});
    in.topic = Validation::opt_string(body, "topic").value_or(std::string{});
    in.tags = std::move(tags);
    return true;
}

bool PostsController::merge_input(const json& body,
                                  const Domain::Post& existing,
                                  Repositories::PostInput& in,
                                  const std::function<void(const HttpResponsePtr&)>& callback) {
    Validation::Errors errs;
    validate_present_fields(body, errs);
    const bool has_tags = body.contains("tags") && !body["tags"].is_null();
    std::vector<std::string> tags;
    if (has_tags)
        tags = validate_tags(body, errs);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return false;
    }
    auto keep_str = [&](const char* k, const std::string& cur) {
        return (body.contains(k) && body[k].is_string()) ? body[k].get<std::string>() : cur;
    };
    in.slug = keep_str("slug", existing.slug);
    in.title = keep_str("title", existing.title);
    in.summary = keep_str("summary", existing.summary);
    in.body = keep_str("body", existing.body);
    in.status = keep_str("status", existing.status);
    in.topic = keep_str("topic", existing.topic);
    in.tags = has_tags ? std::move(tags) : existing.tags;
    return true;
}

}  // namespace Api
