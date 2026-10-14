/**
 * @file ProjectsController.hpp
 * @brief /api/v1/tasks/projects: the owner's projects, which group tasks, and
 *        the courses of Learning (projects of kind "course")
 *        (docs/superpowers/specs/2026-10-14-projects-design.md).
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class ProjectsController : public HttpController<ProjectsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ProjectsController::listProjects, "/api/v1/tasks/projects", Get);
    ADD_METHOD_TO(ProjectsController::createProject, "/api/v1/tasks/projects", Post);
    ADD_METHOD_TO(ProjectsController::getProject, "/api/v1/tasks/projects/{id}", Get);
    ADD_METHOD_TO(ProjectsController::updateProject, "/api/v1/tasks/projects/{id}", Patch);
    ADD_METHOD_TO(ProjectsController::deleteProject, "/api/v1/tasks/projects/{id}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void listProjects(const HttpRequestPtr& req, Callback&& callback);
    void createProject(const HttpRequestPtr& req, Callback&& callback);
    void getProject(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateProject(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteProject(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
};

}  // namespace Api
