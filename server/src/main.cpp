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
#include "engine.hpp"
#include "meta.hpp"

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
    kisa::registerEngine(server, kapp);

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
