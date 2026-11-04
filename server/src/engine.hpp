#pragma once
#include <QHttpServer>
#include "app.hpp"

namespace kisa {
// Движок заявок + документы-минимум + гости.
// Переходы только через transitions-эндпоинт, кнопки клиенты рисуют
// из allowedActions. Гварды, proxy/guest, reopen-лимит — из шаблона.
void registerEngine(QHttpServer &s, App &app);
// Сид workflow_templates:service_request v1 (published) + базовые роли.
// Вызывается из App::seed для свежего тенанта.
void seedServiceRequest(App &app, const bsoncxx::oid &tenantId);
// Идемпотентные дефолты для любого тенанта (роли + шаблон). Вызывается
// из App::seed для ВСЕХ тенантов при старте — так существующий demo
// получит шаблон без пересоздания базы.
void ensureTenantDefaults(App &app, const bsoncxx::oid &tenantId);
} // namespace kisa
