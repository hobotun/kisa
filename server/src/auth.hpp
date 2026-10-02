#pragma once
#include <QHttpServer>
#include "app.hpp"

namespace kisa {
// Регистрирует /api/v1/auth/* и /api/v1/me (настоящие, Mongo+argon2+сессии).
void registerAuth(QHttpServer &s, App &app);
} // namespace kisa
