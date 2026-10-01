// КИСА, server-скелет: Qt HttpServer, tenant-middleware, auth-стабы, мета-CRUD-стабы.
// Контракт из ревью: base /api/v1, tenant из (поддомен + X-Tenant + claim) обязаны совпасть,
// tenantId никогда не берется из тела запроса. Хранилище (mongocxx) — следующий шаг,
// пока ручки отдают 501 not_implemented, живые только /healthz и /openapi.yaml.
#include <QCoreApplication>
#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>

namespace {

QHttpServerResponse jsonResp(const QJsonObject &o,
                             QHttpServerResponse::StatusCode code =
                                 QHttpServerResponse::StatusCode::Ok) {
  return QHttpServerResponse("application/json", QJsonDocument(o).toJson(), code);
}

QHttpServerResponse notImplemented(const QString &what) {
  return jsonResp(
      {{"error", QJsonObject{{"code", "not_implemented"},
                             {"message", "TODO next step: " + what}}}},
      QHttpServerResponse::StatusCode::NotImplemented);
}

QHttpServerResponse unauthorized(const QString &msg = "missing credentials") {
  return jsonResp({{"error", QJsonObject{{"code", "unauthorized"}, {"message", msg}}}},
                  QHttpServerResponse::StatusCode::Unauthorized);
}

// Tenant-middleware: X-Tenant обязателен; поддомен сверяем best-effort;
// сверку с JWT-claim доделать вместе с auth (TODO).
// Возвращает slug или пустую строку (тогда caller отвечает 400).
QString resolveTenant(const QHttpServerRequest &req) {
  // Qt 6.4: значение заголовка — через QHttpServerRequest::value(),
  // headers() там возвращает QList пар без .value().
  const QString headerTenant = QString::fromUtf8(req.value("X-Tenant"));
  if (headerTenant.isEmpty())
    return {};
  // TODO: сравнить с поддоменом Host и claim access-токена (все три обязаны совпасть).
  return headerTenant;
}

QHttpServerResponse requireTenant(const QHttpServerRequest &req, QString &tenantOut) {
  tenantOut = resolveTenant(req);
  if (tenantOut.isEmpty()) {
    return jsonResp(
        {{"error", QJsonObject{{"code", "tenant_required"},
                               {"message", "set X-Tenant header (and tenant subdomain)"}}}},
        QHttpServerResponse::StatusCode::BadRequest);
  }
  return QHttpServerResponse(QHttpServerResponse::StatusCode::Continue);
}

bool hasAuthHeader(const QHttpServerRequest &req) {
  return !req.value("Authorization").isEmpty();
}

QHttpServerResponse notImplementedAuth(const QHttpServerRequest &req, const QString &what) {
  if (!hasAuthHeader(req))
    return unauthorized();
  // TODO: verify JWT, извлечь userId/roles/tenantClaim, сверить tenantClaim == X-Tenant.
  return notImplemented(what);
}

void registerRoutes(QHttpServer &s) {
  s.route("/healthz", QHttpServerRequest::Method::Get, [] {
    return jsonResp({{"status", "ok"}, {"service", "kisa-server"}});
  });

  s.route("/openapi.yaml", QHttpServerRequest::Method::Get, [] {
    // TODO: полный контракт из ревью (auth, meta, documents, requests, guests, ws).
    return QHttpServerResponse("text/yaml", "openapi: 3.0.3\ninfo:\n  title: KISA API\n  version: 0.1.0\n");
  });

  // --- auth ---
  s.route("/api/v1/auth/resolve", QHttpServerRequest::Method::Post,
          [](const QHttpServerRequest &req) {
            QString t;
            if (auto r = requireTenant(req, t); r.statusCode() != QHttpServerResponse::StatusCode::Continue)
              return r;
            return notImplemented("auth/resolve: tenants lookup");
          });
  s.route("/api/v1/auth/login", QHttpServerRequest::Method::Post,
          [](const QHttpServerRequest &req) {
            QString t;
            if (auto r = requireTenant(req, t); r.statusCode() != QHttpServerResponse::StatusCode::Continue)
              return r;
            // TODO: users{tenantId,login} + argon2 + refresh_tokens + rate-limit/lockout.
            return notImplemented("auth/login");
          });
  s.route("/api/v1/auth/refresh", QHttpServerRequest::Method::Post, [](const QHttpServerRequest &) {
    return notImplemented("auth/refresh");
  });
  s.route("/api/v1/auth/logout", QHttpServerRequest::Method::Post, [](const QHttpServerRequest &) {
    return notImplemented("auth/logout");
  });
  s.route("/api/v1/me", QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) {
    return notImplementedAuth(req, "me");
  });

  // --- meta (tenant_admin) ---
  for (const auto path : {"/api/v1/admin/doc-templates", "/api/v1/admin/workflow-templates",
                          "/api/v1/admin/roles", "/api/v1/admin/branches"}) {
    s.route(path, QHttpServerRequest::Method::Get, [path](const QHttpServerRequest &req) {
      return notImplementedAuth(req, QString("list %1 (scoped by tenant)").arg(path));
    });
    s.route(path, QHttpServerRequest::Method::Post, [path](const QHttpServerRequest &req) {
      return notImplementedAuth(req, QString("create %1 (versioned)").arg(path));
    });
  }
  s.route("/api/v1/workflows", QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) {
    return notImplementedAuth(req, "published workflow template ?kind=");
  });

  // --- documents ---
  s.route("/api/v1/documents", QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) {
    return notImplementedAuth(req, "documents list (RBAC filter, counters pinned version)");
  });
  s.route("/api/v1/documents", QHttpServerRequest::Method::Post, [](const QHttpServerRequest &req) {
    return notImplementedAuth(req, "documents create (validate vs doc_templates)");
  });
  s.route("/api/v1/documents/<arg>", QHttpServerRequest::Method::Get,
          [](const QString &, const QHttpServerRequest &req) {
            return notImplementedAuth(req, "documents detail");
          });

  // --- requests (движок; единственный переход — transitions) ---
  s.route("/api/v1/requests", QHttpServerRequest::Method::Get, [](const QHttpServerRequest &req) {
    return notImplementedAuth(req, "requests list (state/assignee/priority/overdue)");
  });
  s.route("/api/v1/requests", QHttpServerRequest::Method::Post, [](const QHttpServerRequest &req) {
    return notImplementedAuth(req, "requests create service_request + submit");
  });
  s.route("/api/v1/requests/<arg>", QHttpServerRequest::Method::Get,
          [](const QString &, const QHttpServerRequest &req) {
            return notImplementedAuth(req, "requests detail + allowedActions + history");
          });
  s.route("/api/v1/requests/<arg>/transitions", QHttpServerRequest::Method::Post,
          [](const QString &, const QHttpServerRequest &req) {
            // TODO движок: tenant, роль, requiredFields, гварды, терминальность, proxy/guest.
            return notImplementedAuth(req, "requests transition");
          });
  s.route("/api/v1/requests/<arg>/mode", QHttpServerRequest::Method::Post,
          [](const QString &, const QHttpServerRequest &req) {
            return notImplementedAuth(req, "requests approvalMode proxy|guest + link");
          });
  s.route("/api/v1/requests/<arg>/comments", QHttpServerRequest::Method::Post,
          [](const QString &, const QHttpServerRequest &req) {
            return notImplementedAuth(req, "requests comment");
          });

  // --- гости (публичный контур, rate-limit TODO) ---
  s.route("/g/<arg>", QHttpServerRequest::Method::Get, [](const QString &) {
    return notImplemented("guest card (tokenHash, scope, TTL)");
  });
  s.route("/g/<arg>/transitions", QHttpServerRequest::Method::Post, [](const QString &) {
    return notImplemented("guest transition (single-use action)");
  });
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const quint16 port =
      QString::fromLocal8Bit(qgetenv("PORT")).toUShort() ?: quint16(8080);

  QHttpServer server;
  registerRoutes(server);

  QTcpServer tcp;
  if (!tcp.listen(QHostAddress::Any, port)) {
    qCritical("cannot listen on %hu", port);
    return 1;
  }
  server.bind(&tcp);
  qInfo("kisa-server on %hu (MONGO_URI %s)", port,
        qgetenv("MONGO_URI").isEmpty() ? "(storage TODO)" : "(storage TODO)");
  return app.exec();
}
