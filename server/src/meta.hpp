#pragma once
#include <QHttpServer>
#include "app.hpp"

namespace kisa {
// Мета-CRUD в скоупе тенанта (только tenant_admin):
// doc-templates и workflow-templates — версионные (create/get/list/новая версия/publish).
// roles/branches — list/create минимум.
void registerMeta(QHttpServer &s, App &app);
} // namespace kisa
