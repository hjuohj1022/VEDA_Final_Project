#pragma once

#include "crow.h"

#include "features/common/RequestAuthorizer.h"

// Registers the authenticated event-log CRUD routes.
void registerEventLogRoutes(crow::SimpleApp& app, const RequestAuthorizer& is_authorized);
