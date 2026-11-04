#pragma once
// Общее ядро сервера: Mongo pool, сессии, tenant-middleware, JSON-утилиты.
// Токены — opaque (без JWT-библиотеки): случайные 256 бит, в Mongo только sha256.
// Пароли — argon2id. tenantId подставляется сервером из сессии, никогда из тела.
// mongocxx::pool используется вместо одного client: Qt HttpServer конкурентен.
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <bsoncxx/oid.hpp>
#include <mongocxx/database.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>

#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonObject>

namespace kisa {

using Clock = std::chrono::system_clock;
inline constexpr auto ACCESS_TTL  = std::chrono::minutes(15);
inline constexpr auto REFRESH_TTL = std::chrono::hours(24 * 30);

struct Principal {
    bsoncxx::oid tenantOid;
    bsoncxx::oid userOid;
    std::string tenantId; // hex, для JSON-ответов
    std::string userId;   // hex, для JSON-ответов
    std::string login;
    std::string name;
    std::vector<std::string> roles;
};

struct App {
    mongocxx::instance mongoInit; // должна жить дольше pool
    mongocxx::pool pool;

    // примитивный rate-limit логина: ключ tenant/login -> {fails, until}
    std::mutex loginMu;
    std::unordered_map<std::string, std::pair<int, Clock::time_point>> loginFails;

    explicit App(const std::string &mongoUri);
    void ensureIndexes();
    void seed();

    mongocxx::pool::entry acquire() { return pool.acquire(); }
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
std::string randHex(std::size_t bytes);
QString sha256Hex(const QByteArray &data);
QString argon2Hash(const QString &password);
bool argon2Verify(const std::string &encoded, const QString &password);

// --- tenant / principal ---
// db — уже приобретённый клиент из pool; tenantId никогда не берётся из тела.
std::string tenantIdFor(mongocxx::database &db, const QHttpServerRequest &req);
std::optional<Principal> principalFor(mongocxx::database &db,
                                      const QHttpServerRequest &req);
bool hasRole(const Principal &p, const char *role);

} // namespace kisa
