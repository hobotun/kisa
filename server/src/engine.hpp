#pragma once
#include <QHttpServer>
#include <mongocxx/database.hpp>
#include "app.hpp"

namespace kisa {
void registerEngine(QHttpServer &s, App &app);
// seedServiceRequest и ensureTenantDefaults принимают db (уже приобретённый из pool).
void seedServiceRequest(mongocxx::database &db, const bsoncxx::oid &tenantId);
void ensureTenantDefaults(mongocxx::database &db, const bsoncxx::oid &tenantId);
} // namespace kisa
