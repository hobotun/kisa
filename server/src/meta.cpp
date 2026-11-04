#include "meta.hpp"

#include <algorithm>
#include <optional>

#include <QJsonArray>
#include <QUrlQuery>

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>

namespace kisa {
namespace b = bsoncxx::builder::basic;
using b::kvp;

namespace {

bsoncxx::types::bson_value::value jsonToBson(const QJsonValue &v);
bsoncxx::document::value jsonObjToBson(const QJsonObject &o) {
    b::document d;
    for (auto it = o.begin(); it != o.end(); ++it)
        d.append(kvp(it.key().toStdString(), jsonToBson(it.value())));
    return d.extract();
}
bsoncxx::types::bson_value::value jsonToBson(const QJsonValue &v) {
    using V = bsoncxx::types::bson_value::value;
    if (v.isBool())   return V(v.toBool());
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (d == (double)(int32_t)d) return V((int32_t)d);
        return V(d);
    }
    if (v.isString()) return V(v.toString().toStdString());
    if (v.isArray()) {
        b::array a;
        for (const auto &e : v.toArray()) a.append(jsonToBson(e));
        return V(a.extract());
    }
    if (v.isObject()) return V(jsonObjToBson(v.toObject()));
    return V(bsoncxx::types::b_null{});
}

std::optional<Principal> requireAdmin(mongocxx::database &db,
                                      const QHttpServerRequest &req,
                                      QHttpServerResponse &deny) {
    auto p = principalFor(db, req);
    if (!p) {
        deny = errResp("unauthorized", "invalid or expired token",
                       QHttpServerResponse::StatusCode::Unauthorized);
        return std::nullopt;
    }
    if (!hasRole(*p, "tenant_admin")) {
        deny = errResp("forbidden", "tenant_admin required",
                       QHttpServerResponse::StatusCode::Forbidden);
        return std::nullopt;
    }
    return p;
}

int limitOf(const QHttpServerRequest &req) {
    bool ok = false;
    const int l = QUrlQuery(req.url()).queryItemValue("limit").toInt(&ok);
    return (ok && l > 0) ? std::min(l, 100) : 20;
}
int skipOf(const QHttpServerRequest &req) {
    bool ok = false;
    const int p = QUrlQuery(req.url()).queryItemValue("page").toInt(&ok);
    return (ok && p > 1) ? (p - 1) * limitOf(req) : 0;
}

bool validKey(const QString &k) {
    if (k.isEmpty() || k.size() > 64) return false;
    for (const QChar c : k)
        if (!(c.isLetterOrNumber() || c == '_' || c == '-')) return false;
    return true;
}

void registerTemplateCrud(QHttpServer &s, App &app, const char *coll, const char *base) {
    const QString list = QString("%1").arg(base);
    const QString one  = QString("%1/<arg>").arg(base);

    s.route(list, QHttpServerRequest::Method::Get,
            [&app, coll](const QHttpServerRequest &req) {
                auto entry = app.acquire();
                auto db = (*entry)[std::string("kisa")];
                QHttpServerResponse deny(QHttpServerResponse::StatusCode::Unauthorized);
                auto p = requireAdmin(db, req, deny);
                if (!p) return deny;
                try {
                    b::document filter;
                    filter.append(kvp("tenantId", p->tenantOid));
                    const QString st = QUrlQuery(req.url()).queryItemValue("status");
                    if (!st.isEmpty())
                        filter.append(kvp("status", st.toStdString()));
                    mongocxx::options::find opts;
                    opts.limit(limitOf(req)).skip(skipOf(req));
                    opts.sort(b::make_document(kvp("key", 1), kvp("version", 1)));
                    QJsonArray items;
                    for (const auto &d : db[coll].find(filter.extract(), opts))
                        items.append(docToJson(d));
                    QJsonObject resp;
                    resp["items"] = items;
                    return jsonResp(resp);
                } catch (...) {
                    return errResp("internal", "list failed",
                                   QHttpServerResponse::StatusCode::InternalServerError);
                }
            });

    s.route(list, QHttpServerRequest::Method::Post,
            [&app, coll](const QHttpServerRequest &req) {
                auto entry = app.acquire();
                auto db = (*entry)[std::string("kisa")];
                QHttpServerResponse deny(QHttpServerResponse::StatusCode::Unauthorized);
                auto p = requireAdmin(db, req, deny);
                if (!p) return deny;
                bool ok = false;
                const QJsonObject body = reqJson(req, ok);
                if (!ok || !validKey(body.value("key").toString()))
                    return errResp("bad_request", "key required [a-z0-9_-]",
                                   QHttpServerResponse::StatusCode::BadRequest);
                try {
                    b::document d;
                    d.append(kvp("tenantId", p->tenantOid));
                    d.append(kvp("key",     body.value("key").toString().toStdString()));
                    d.append(kvp("version", (int32_t)1));
                    d.append(kvp("status",  "draft"));
                    d.append(kvp("title",   body.value("title").toString().toStdString()));
                    for (const char *f : {"fields", "states", "transitions", "numbering",
                                          "slaByPriority", "instruction"})
                        if (body.contains(f))
                            d.append(kvp(std::string(f), jsonToBson(body.value(f))));
                    d.append(kvp("createdBy", p->userOid));
                    d.append(kvp("createdAt", bsoncxx::types::b_date(Clock::now())));
                    auto r = db[coll].insert_one(d.extract());
                    auto got = db[coll].find_one(b::make_document(
                        kvp("_id", r->inserted_id().get_oid().value)));
                    return jsonResp(docToJson(got->view()));
                } catch (const std::exception &e) {
                    const std::string w = e.what();
                    if (w.find("E11000") != std::string::npos ||
                        w.find("duplicate key") != std::string::npos)
                        return errResp("conflict", "key already exists",
                                       QHttpServerResponse::StatusCode::Conflict);
                    return errResp("internal", "create failed",
                                   QHttpServerResponse::StatusCode::InternalServerError);
                } catch (...) {
                    return errResp("internal", "create failed",
                                   QHttpServerResponse::StatusCode::InternalServerError);
                }
            });

    s.route(one, QHttpServerRequest::Method::Get,
            [&app, coll](const QString &key, const QHttpServerRequest &req) {
                auto entry = app.acquire();
                auto db = (*entry)[std::string("kisa")];
                QHttpServerResponse deny(QHttpServerResponse::StatusCode::Unauthorized);
                auto p = requireAdmin(db, req, deny);
                if (!p) return deny;
                try {
                    mongocxx::options::find opts;
                    opts.sort(b::make_document(kvp("version", 1)));
                    QJsonArray items;
                    for (const auto &d : db[coll].find(
                             b::make_document(kvp("tenantId", p->tenantOid),
                                              kvp("key", key.toStdString())), opts))
                        items.append(docToJson(d));
                    if (items.isEmpty())
                        return errResp("not_found", "no such template",
                                       QHttpServerResponse::StatusCode::NotFound);
                    QJsonObject resp;
                    resp["items"] = items;
                    return jsonResp(resp);
                } catch (...) {
                    return errResp("internal", "get failed",
                                   QHttpServerResponse::StatusCode::InternalServerError);
                }
            });
}

} // namespace

void registerMeta(QHttpServer &s, App &app) {
    registerTemplateCrud(s, app, "doc_templates",      "/api/v1/admin/doc-templates");
    registerTemplateCrud(s, app, "workflow_templates", "/api/v1/admin/workflow-templates");

    for (const char *base : {"/api/v1/admin/doc-templates",
                              "/api/v1/admin/workflow-templates"}) {
        const QString pub = QString("%1/<arg>/versions/<arg>/publish").arg(base);
        const std::string collName =
            (base == std::string("/api/v1/admin/doc-templates"))
                ? "doc_templates" : "workflow_templates";
        s.route(pub, QHttpServerRequest::Method::Post,
                [&app, collName](const QString &key, const QString &ver,
                                 const QHttpServerRequest &req) {
                    auto entry = app.acquire();
                    auto db = (*entry)[std::string("kisa")];
                    QHttpServerResponse deny(QHttpServerResponse::StatusCode::Unauthorized);
                    auto p = requireAdmin(db, req, deny);
                    if (!p) return deny;
                    try {
                        auto r = db[collName].update_one(
                            b::make_document(
                                kvp("tenantId", p->tenantOid),
                                kvp("key",      key.toStdString()),
                                kvp("version",  (int32_t)ver.toInt())),
                            b::make_document(kvp("$set", b::make_document(
                                kvp("status", "published")))));
                        if (r->modified_count() == 0)
                            return errResp("not_found", "no such version",
                                           QHttpServerResponse::StatusCode::NotFound);
                        QJsonObject resp;
                        resp["ok"] = true;
                        return jsonResp(resp);
                    } catch (...) {
                        return errResp("internal", "publish failed",
                                       QHttpServerResponse::StatusCode::InternalServerError);
                    }
                });
    }

    s.route("/api/v1/admin/roles", QHttpServerRequest::Method::Get,
            [&app](const QHttpServerRequest &req) {
                auto entry = app.acquire();
                auto db = (*entry)[std::string("kisa")];
                QHttpServerResponse deny(QHttpServerResponse::StatusCode::Unauthorized);
                auto p = requireAdmin(db, req, deny);
                if (!p) return deny;
                QJsonArray items;
                for (const auto &d : db["roles"].find(
                         b::make_document(kvp("tenantId", p->tenantOid))))
                    items.append(docToJson(d));
                QJsonObject resp;
                resp["items"] = items;
                return jsonResp(resp);
            });

    s.route("/api/v1/admin/branches", QHttpServerRequest::Method::Get,
            [&app](const QHttpServerRequest &req) {
                auto entry = app.acquire();
                auto db = (*entry)[std::string("kisa")];
                QHttpServerResponse deny(QHttpServerResponse::StatusCode::Unauthorized);
                auto p = requireAdmin(db, req, deny);
                if (!p) return deny;
                QJsonArray items;
                for (const auto &d : db["branches"].find(
                         b::make_document(kvp("tenantId", p->tenantOid))))
                    items.append(docToJson(d));
                QJsonObject resp;
                resp["items"] = items;
                return jsonResp(resp);
            });
}

} // namespace kisa
