#include "auth.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>

namespace kisa {
namespace b = bsoncxx::builder::basic;
using b::kvp;

namespace {

// Выдача пары токенов + запись сессии. Возвращает тело ответа.
QJsonObject issueSession(App &app, const bsoncxx::oid &tid, const bsoncxx::oid &uid,
                         const QString &device) {
  const auto now = Clock::now();
  const std::string access = randHex(32);
  const std::string refresh = randHex(32);
  app.db["sessions"].insert_one(b::make_document(
      kvp("tenantId", tid), kvp("userId", uid),
      kvp("accessHash", std::string(sha256Hex(QByteArray::fromStdString(access)).toLatin1().constData())),
      kvp("refreshHash", std::string(sha256Hex(QByteArray::fromStdString(refresh)).toLatin1().constData())),
      kvp("accessExp", bsoncxx::types::b_date(now + ACCESS_TTL)),
      kvp("refreshExp", bsoncxx::types::b_date(now + REFRESH_TTL)),
      kvp("device", std::string(device.toUtf8().constData())), kvp("createdAt", bsoncxx::types::b_date(now))));
  return {{"accessToken", QString::fromStdString(access)},
          {"refreshToken", QString::fromStdString(refresh)},
          {"expiresIn", (int)std::chrono::duration_cast<std::chrono::seconds>(ACCESS_TTL).count()}};
}

bool loginLocked(App &app, const std::string &key) {
  std::lock_guard<std::mutex> g(app.loginMu);
  auto it = app.loginFails.find(key);
  if (it == app.loginFails.end())
    return false;
  if (Clock::now() < it->second.second)
    return true;
  app.loginFails.erase(it);
  return false;
}

void loginFail(App &app, const std::string &key) {
  std::lock_guard<std::mutex> g(app.loginMu);
  int n = ++app.loginFails[key].first;
  if (n >= 5)
    app.loginFails[key].second = Clock::now() + std::chrono::minutes(5);
}

void loginOk(App &app, const std::string &key) {
  std::lock_guard<std::mutex> g(app.loginMu);
  app.loginFails.erase(key);
}

} // namespace

void registerAuth(QHttpServer &s, App &app) {
  // Шаг 1 логина: тенант по slug.
  s.route("/api/v1/auth/resolve", QHttpServerRequest::Method::Post,
          [&app](const QHttpServerRequest &req) {
            const std::string tid = tenantIdFor(app, req);
            if (tid.empty())
              return errResp("unknown_tenant", "tenant not found",
                             QHttpServerResponse::StatusCode::NotFound);
            try {
              auto t = app.db["tenants"].find_one(
                  b::make_document(kvp("_id", bsoncxx::oid{tid})));
              const auto tv = t->view();
              return jsonResp({{"slug", QString::fromUtf8(req.value("X-Tenant"))},
                               {"name", QString::fromStdString(
                                            std::string(tv["name"].get_string().value))}});
            } catch (...) {
              return errResp("unknown_tenant", "tenant not found",
                             QHttpServerResponse::StatusCode::NotFound);
            }
          });

  // Шаг 2: логин + пара токенов.
  s.route("/api/v1/auth/login", QHttpServerRequest::Method::Post,
          [&app](const QHttpServerRequest &req) {
            const std::string tid = tenantIdFor(app, req);
            if (tid.empty())
              return errResp("unknown_tenant", "tenant not found",
                             QHttpServerResponse::StatusCode::NotFound);
            bool ok = false;
            const QJsonObject body = reqJson(req, ok);
            if (!ok)
              return errResp("bad_request", "invalid JSON",
                             QHttpServerResponse::StatusCode::BadRequest);
            const QString login = body.value("login").toString().trimmed();
            const QString password = body.value("password").toString();
            const QString device = body.value("device").toString().left(200);
            if (login.isEmpty() || password.isEmpty())
              return errResp("bad_request", "login+password required",
                             QHttpServerResponse::StatusCode::BadRequest);
            const std::string lkey = tid + "/" + login.toStdString();
            if (loginLocked(app, lkey))
              return errResp("locked", "too many attempts, try later",
                             QHttpServerResponse::StatusCode::TooManyRequests);
            try {
              auto u = app.db["users"].find_one(b::make_document(
                  kvp("tenantId", bsoncxx::oid{tid}),
                  kvp("login", login.toStdString())));
              bool good = false;
              if (u) {
                const auto uv = u->view();
                good = std::string(uv["status"].get_string().value) == "active" &&
                       argon2Verify(std::string(uv["passwordHash"].get_string().value), password);
              }
              if (!good) {
                loginFail(app, lkey);
                return errResp("bad_credentials", "invalid login or password",
                               QHttpServerResponse::StatusCode::Unauthorized);
              }
              loginOk(app, lkey);
              const auto uv = u->view();
              QJsonObject resp = issueSession(app, bsoncxx::oid{tid},
                                              uv["_id"].get_oid().value, device);
              QJsonArray roles;
              for (const auto &r : uv["roles"].get_array().value)
                roles.append(QString::fromStdString(std::string(r.get_string().value)));
              resp.insert("user", QJsonObject{{"login", login},
                                              {"name", QString::fromStdString(
                                                           std::string(uv["name"].get_string().value))},
                                              {"roles", roles}});
              return jsonResp(resp);
            } catch (...) {
              return errResp("internal", "login failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  // Ротация пары по refresh (refresh одноразовый).
  s.route("/api/v1/auth/refresh", QHttpServerRequest::Method::Post,
          [&app](const QHttpServerRequest &req) {
            bool ok = false;
            const QJsonObject body = reqJson(req, ok);
            if (!ok)
              return errResp("bad_request", "invalid JSON",
                             QHttpServerResponse::StatusCode::BadRequest);
            const QString rh = sha256Hex(body.value("refreshToken").toString().toUtf8());
            try {
              auto col = app.db["sessions"];
              auto sess = col.find_one(b::make_document(
                  kvp("refreshHash", std::string(rh.toLatin1().constData()))));
              if (!sess)
                return errResp("bad_token", "unknown refresh token",
                               QHttpServerResponse::StatusCode::Unauthorized);
              const auto sv = sess->view();
              if (kisa::Clock::time_point(sv["refreshExp"].get_date().value) <
                  kisa::Clock::now())
                return errResp("expired", "refresh token expired",
                               QHttpServerResponse::StatusCode::Unauthorized);
              const bsoncxx::oid tid = sv["tenantId"].get_oid().value;
              const bsoncxx::oid uid = sv["userId"].get_oid().value;
              col.delete_one(b::make_document(kvp("_id", sv["_id"].get_oid().value)));
              return jsonResp(issueSession(app, tid, uid, "refresh"));
            } catch (...) {
              return errResp("internal", "refresh failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  // Logout: отозвать текущую сессию.
  s.route("/api/v1/auth/logout", QHttpServerRequest::Method::Post,
          [&app](const QHttpServerRequest &req) {
            const QByteArray auth = req.value("Authorization");
            if (auth.startsWith("Bearer ")) {
              const QString ah = sha256Hex(auth.mid(7));
              try {
                app.db["sessions"].delete_one(b::make_document(
                    kvp("accessHash", std::string(ah.toLatin1().constData()))));
              } catch (...) {
              }
            }
            return jsonResp({{"ok", true}});
          });

  // Профиль + роли.
  s.route("/api/v1/me", QHttpServerRequest::Method::Get,
          [&app](const QHttpServerRequest &req) {
            auto p = principalFor(app, req);
            if (!p)
              return errResp("unauthorized", "invalid or expired token",
                             QHttpServerResponse::StatusCode::Unauthorized);
            QJsonArray roles;
            for (const auto &r : p->roles)
              roles.append(QString::fromStdString(r));
            return jsonResp({{"login", QString::fromStdString(p->login)},
                             {"name", QString::fromStdString(p->name)},
                             {"roles", roles},
                             {"tenantId", QString::fromStdString(p->tenantId)}});
          });
}

} // namespace kisa
