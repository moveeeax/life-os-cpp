/**
 * @file TasksController.hpp
 * @brief The /api/v1/tasks routes: the person's tasks, the agenda of one
 *        local day, the inbox of notes and the one-phrase parse. Every
 *        handler checks the module switch, then the caller's user id; rows
 *        belong to that user only.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class TasksController : public HttpController<TasksController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TasksController::status, "/api/v1/tasks/status", Get);
    ADD_METHOD_TO(TasksController::listItems, "/api/v1/tasks/items", Get);
    ADD_METHOD_TO(TasksController::createItem, "/api/v1/tasks/items", Post);
    ADD_METHOD_TO(TasksController::getItem, "/api/v1/tasks/items/{id}", Get);
    ADD_METHOD_TO(TasksController::updateItem, "/api/v1/tasks/items/{id}", Patch);
    ADD_METHOD_TO(TasksController::deleteItem, "/api/v1/tasks/items/{id}", Delete);
    ADD_METHOD_TO(TasksController::agenda, "/api/v1/tasks/agenda", Get);
    ADD_METHOD_TO(TasksController::listNotes, "/api/v1/tasks/notes", Get);
    ADD_METHOD_TO(TasksController::createNote, "/api/v1/tasks/notes", Post);
    ADD_METHOD_TO(TasksController::updateNote, "/api/v1/tasks/notes/{id}", Patch);
    ADD_METHOD_TO(TasksController::deleteNote, "/api/v1/tasks/notes/{id}", Delete);
    ADD_METHOD_TO(TasksController::parse, "/api/v1/tasks/parse", Post);
    ADD_METHOD_TO(TasksController::parseStatus, "/api/v1/tasks/parse/{id}", Get);
    ADD_METHOD_TO(TasksController::parseAccept, "/api/v1/tasks/parse/{id}/accept", Post);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void status(const HttpRequestPtr& req, Callback&& callback);
    void listItems(const HttpRequestPtr& req, Callback&& callback);
    void createItem(const HttpRequestPtr& req, Callback&& callback);
    void getItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void agenda(const HttpRequestPtr& req, Callback&& callback);
    void listNotes(const HttpRequestPtr& req, Callback&& callback);
    void createNote(const HttpRequestPtr& req, Callback&& callback);
    void updateNote(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteNote(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void parse(const HttpRequestPtr& req, Callback&& callback);
    void parseStatus(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void parseAccept(const HttpRequestPtr& req, Callback&& callback, const std::string& id);

private:
    static bool require_enabled(const Callback& callback);
};

}  // namespace Api
