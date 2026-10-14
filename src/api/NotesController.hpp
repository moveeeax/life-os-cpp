/**
 * @file NotesController.hpp
 * @brief /api/v1/notes: the owner's private Markdown notes, shaped like the
 *        tarassov.me blog posts so a note can be sent there later
 *        (docs/superpowers/specs/2026-10-14-notes-design.md).
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class NotesController : public HttpController<NotesController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(NotesController::listNotes, "/api/v1/notes", Get);
    ADD_METHOD_TO(NotesController::createNote, "/api/v1/notes", Post);
    ADD_METHOD_TO(NotesController::getNote, "/api/v1/notes/{id}", Get);
    ADD_METHOD_TO(NotesController::updateNote, "/api/v1/notes/{id}", Patch);
    ADD_METHOD_TO(NotesController::deleteNote, "/api/v1/notes/{id}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void listNotes(const HttpRequestPtr& req, Callback&& callback);
    void createNote(const HttpRequestPtr& req, Callback&& callback);
    void getNote(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateNote(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteNote(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
};

}  // namespace Api
