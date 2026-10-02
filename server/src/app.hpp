#pragma once
// Общее ядро сервера: Mongo, сессии, tenant-middleware, JSON-утилиты.
// Токены — opaque (без JWT-библиотеки): случайные 256 бит, в Mongo только sha256.
// Пароли — argon2id. tenantId подставляется сервером из сессии, никогда из тела.
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <mongocxx/client.hpp>
#include <mongocxx/collection.hpp>
#include <mongocxx/database.hpp>
#include <mongocxx/instance.hpp>

#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonObject>

namespace kisa {

using Clock = std::chrono::system_clock;
inline constexpr auto ACCESS_TTL = std::chrono::minutes(15);
inline constexpr auto REFRESH_TTL = std::chrono::hours(24 * 30);

struct Principal {
  std::string tenantId; // hex ObjectId
  std::string userId;   // hex ObjectId
  std::string login;
  std::string name;
  std::vector<std::string> roles;
};

struct App {
  mongocxx::instance mongoInit; // должна жить дольше всего
  mongocxx::client client;
  mongocxx::database db;

  // примитивный rate-limit логина: ключ tenant/login -> {fails, until}
  std::mutex loginMu;
  std::unordered_map<std::string, std::pair<int, Clock::time_point>> loginFails;

  explicit App(const std::string &mongoUri);
  void ensureIndexes();
  void seed(); // первый тенант+админ из env, только если tenants пусто
};

// --- JSON ---
QHttpServerResponse jsonResp(const QJsonObject &o,
                             QHttpServerResponse::StatusCode code =
                                 QHttpServerResponse::StatusCode::Ok);
QHttpServerResponse errResp(const QString &code, const QString &msg,
                            QHttpServerResponse::StatusCode http);
QJsonObject reqJson(const QHttpServerRequest &req, bool &ok);

// --- BSON -> QJson через Extended JSON (версионно-независимо) ---
QJsonObject docToJson(const bsoncxx::document::view &d);

// --- crypto ---
std::string randHex(std::size_t bytes); // криптографически стойкие байты
QString sha256Hex(const QByteArray &data);
QString argon2Hash(const QString &password);
bool argon2Verify(const std::string &encoded, const QString &password);

// --- tenant / principal ---
// Возвращает tenant _id (hex) по slug из X-Tenant, "" если нет/не найден.
std::string tenantIdFor(App &app, const QHttpServerRequest &req);
// Principal по Bearer-токену + сверка tenantId сессии с X-Tenant. nullopt = 401.
std::optional<Principal> principalFor(App &app, const QHttpServerRequest &req);
bool hasRole(const Principal &p, const char *role);

} // namespace kisa
