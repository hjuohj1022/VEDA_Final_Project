#ifndef EVENT_LOG_ROUTES_H
#define EVENT_LOG_ROUTES_H

#include "crow.h"

#include <functional>

using EventLogRequestAuthorizer = std::function<bool(const crow::request&)>;

void registerEventLogRoutes(crow::SimpleApp& app, const EventLogRequestAuthorizer& is_authorized);

#endif // EVENT_LOG_ROUTES_H
