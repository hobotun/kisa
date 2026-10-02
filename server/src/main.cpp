// КИСА, server: Qt HttpServer + Mongo (mongocxx) + argon2 + opaque-сессии.
// Контракт из ревью: base /api/v1, tenant из X-Tenant + сессии, tenantId
// подставляется сервером, никогда из тела. Живые: auth, me, meta-CRUD.
// Документы/заявки/гости — следующий слой (движок переходов).
#include <QCoreApplication>
#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>

#include "app.hpp"
#include "auth.hpp"
#include "meta.hpp"

namespace kisa {
namespace {

void registerStubs(QHttpServer &s, App &app) {
  auto needAuth = [&app](const QHttpServerRequest &req, const QString &what) {
    if (!principalFor(app, req))
      return errResp("unauthorized", "invalid or expired token",
                     QHttpServerResponse::StatusCode::Unauthorized);
    return errResp("not_implemented", "TODO next layer: " + what,
                   QHttpServerResponse::StatusCode::NotImplemented);
  };
  s.route("/api/v1/workflows", QHttpServerRequest::Method::Get,
          [needAuth](const QHttpServerRequest &req) {
            return needAuth(req, "published workflow template");
          });
  for (const char *r : {"/api/v1/documents", "/api/v1/requests"}) {
    const QString base(r);
    s.route(base, QHttpServerRequest::Method::Get,
            [needAuth, base](const QHttpServerRequest &req) {
              return needAuth(req, base + " list");
            });
    s.route(base, QHttpServerRequest::Method::Post,
            [needAuth, base](const QHttpServerRequest &req) {
              return needAuth(req, base + " create");
            });
  }
  s.route("/api/v1/documents/<arg>", QHttpServerRequest::Method::Get,
          [needAuth](const QString &, const QHttpServerRequest &req) {
            return needAuth(req, "documents detail");
          });
  s.route("/api/v1/requests/<arg>", QHttpServerRequest::Method::Get,
          [needAuth](const QString &, const QHttpServerRequest &req) {
            return needAuth(req, "requests detail + allowedActions");
          });
  s.route("/api/v1/requests/<arg>/transitions", QHttpServerRequest::Method::Post,
          [needAuth](const QString &, const QHttpServerRequest &req) {
            return needAuth(req, "requests transition");
          });
  s.route("/api/v1/requests/<arg>/mode", QHttpServerRequest::Method::Post,
          [needAuth](const QString &, const QHttpServerRequest &req) {
            return needAuth(req, "requests approvalMode");
          });
  s.route("/api/v1/requests/<arg>/comments", QHttpServerRequest::Method::Post,
          [needAuth](const QString &, const QHttpServerRequest &req) {
            return needAuth(req, "requests comment");
          });
  s.route("/g/<arg>", QHttpServerRequest::Method::Get, [](const QString &) {
    return errResp("not_implemented", "TODO next layer: guest card",
                   QHttpServerResponse::StatusCode::NotImplemented);
  });
  s.route("/g/<arg>/transitions", QHttpServerRequest::Method::Post, [](const QString &) {
    return errResp("not_implemented", "TODO next layer: guest transition",
                   QHttpServerResponse::StatusCode::NotImplemented);
  });
}

} // namespace
} // namespace kisa

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const quint16 port =
      QString::fromLocal8Bit(qgetenv("PORT")).toUShort() ?: quint16(8080);
  const std::string mongoUri = qgetenv("MONGO_URI").toStdString();

  try {
    kisa::App kapp(mongoUri.empty() ? "mongodb://mongo:27017/kisa" : mongoUri);
    kapp.ensureIndexes();
    kapp.seed();

    QHttpServer server;
    server.route("/healthz", QHttpServerRequest::Method::Get, [] {
      return kisa::jsonResp({{"status", "ok"}, {"service", "kisa-server"}});
    });
    server.route("/openapi.yaml", QHttpServerRequest::Method::Get, [] {
      return QHttpServerResponse(
          "text/yaml", "openapi: 3.0.3\ninfo:\n  title: KISA API\n  version: 0.2.0\n");
    });
    kisa::registerAuth(server, kapp);
    kisa::registerMeta(server, kapp);
    kisa::registerStubs(server, kapp);

    const quint16 bound = server.listen(QHostAddress::Any, port);
    if (bound == 0) {
      qCritical("cannot listen on %u", static_cast<unsigned>(port));
      return 1;
    }
    qInfo("kisa-server on %u", static_cast<unsigned>(bound));
    return QCoreApplication::exec();
  } catch (const std::exception &e) {
    qCritical("startup failed: %s", e.what());
    return 1;
  }
}
