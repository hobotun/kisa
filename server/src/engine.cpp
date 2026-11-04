#include "engine.hpp"

#include <QJsonArray>
#include <QUrlQuery>

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/options/find_one_and_update.hpp>

namespace kisa {
namespace b = bsoncxx::builder::basic;
using b::kvp;

namespace {

// QJson -> BSON (только JSON-типы, достаточно для полей/шаблонов).
bsoncxx::types::bson_value::value j2b(const QJsonValue &v);
bsoncxx::document::value j2doc(const QJsonObject &o) {
  b::document d;
  for (auto it = o.begin(); it != o.end(); ++it)
    d.append(kvp(it.key().toStdString(), j2b(it.value())));
  return d.extract();
}
bsoncxx::types::bson_value::value j2b(const QJsonValue &v) {
  using V = bsoncxx::types::bson_value::value;
  if (v.isBool())
    return V(v.toBool());
  if (v.isDouble()) {
    const double x = v.toDouble();
    if (x == (double)(int32_t)x)
      return V((int32_t)x);
    return V(x);
  }
  if (v.isString())
    return V(v.toString().toStdString());
  if (v.isArray()) {
    b::array a;
    for (const auto &e : v.toArray())
      a.append(j2b(e));
    return V(a.extract());
  }
  if (v.isObject())
    return V(j2doc(v.toObject()));
  return V(bsoncxx::types::b_null{});
}

bool parseOid(const QString &s, bsoncxx::oid &out) {
  try {
    out = bsoncxx::oid{s.toStdString()};
    return true;
  } catch (...) {
    return false;
  }
}

// Опубликованный шаблон workflow (макс. версия) для тенанта.
std::optional<bsoncxx::document::value> loadFlow(App &app, const bsoncxx::oid &tid,
                                                 const std::string &key) {
  mongocxx::options::find opts;
  opts.sort(b::make_document(kvp("version", -1)));
  opts.limit(1);
  auto cur = app.db["workflow_templates"].find(
      b::make_document(kvp("tenantId", tid), kvp("key", key), kvp("status", "published")),
      opts);
  for (const auto &d : cur)
    return bsoncxx::document::value(d);
  return std::nullopt;
}

std::string strField(const bsoncxx::document::view &d, const char *k,
                     const std::string &def = {}) {
  const auto e = d[k];
  if (e && e.type() == bsoncxx::type::k_string)
    return std::string(e.get_string().value);
  return def;
}

struct Actor {
  QString type; // user | guest
  std::string userId;
  std::string label;
  std::vector<std::string> roles;
  std::vector<std::string> guestScope;
  std::string onBehalfOf;
};

struct ApplyResult {
  bool ok = false;
  int http = 500;
  QString code;
  QString msg;
  std::string newState;
};

// Поиск перехода по (from, action) в шаблоне. Возвращает view в память шаблона.
std::optional<bsoncxx::document::view>
findTransition(const bsoncxx::document::view &tmpl, const std::string &from,
               const QString &action) {
  const auto trs = tmpl["transitions"];
  if (!trs || trs.type() != bsoncxx::type::k_array)
    return std::nullopt;
  for (const auto &t : trs.get_array().value) {
    const auto td = t.get_document().value;
    if (strField(td, "from") == from &&
        strField(td["action"].get_document().value, "key") == action.toStdString())
      return td;
  }
  return std::nullopt;
}

bool stateTerminal(const bsoncxx::document::view &tmpl, const std::string &state) {
  const auto sts = tmpl["states"];
  if (!sts || sts.type() != bsoncxx::type::k_array)
    return false;
  for (const auto &s : sts.get_array().value) {
    const auto sd = s.get_document().value;
    if (strField(sd, "key") == state) {
      const auto t = sd["terminal"];
      return t && t.type() == bsoncxx::type::k_bool && t.get_bool().value;
    }
  }
  return false;
}

QString stateTitle(const bsoncxx::document::view &tmpl, const std::string &state) {
  const auto sts = tmpl["states"];
  if (sts && sts.type() == bsoncxx::type::k_array)
    for (const auto &s : sts.get_array().value) {
      const auto sd = s.get_document().value;
      if (strField(sd, "key") == state)
        return QString::fromStdString(strField(sd, "title", state));
    }
  return QString::fromStdString(state);
}

// allowedActions для пользователя или гостя.
QJsonArray allowedActions(const bsoncxx::document::view &tmpl, const std::string &state,
                          const Actor &actor) {
  QJsonArray out;
  const auto trs = tmpl["transitions"];
  if (!trs || trs.type() != bsoncxx::type::k_array)
    return out;
  for (const auto &t : trs.get_array().value) {
    const auto td = t.get_document().value;
    if (strField(td, "from") != state)
      continue;
    const auto ad = td["action"].get_document().value;
    const std::string akey = strField(ad, "key");
    bool allowed = false;
    if (actor.type == "guest") {
      for (const auto &s : actor.guestScope)
        if (s == akey) {
          allowed = true;
          break;
        }
    } else {
      const auto rl = td["roles"];
      if (rl && rl.type() == bsoncxx::type::k_array)
        for (const auto &r : rl.get_array().value) {
          const std::string rs(r.get_string().value);
          for (const auto &ur : actor.roles)
            if (ur == rs) {
              allowed = true;
              break;
            }
          if (allowed)
            break;
        }
    }
    if (!allowed)
      continue;
    QJsonObject a{{"action", QString::fromStdString(akey)},
                  {"title", QString::fromStdString(strField(ad, "title", akey))}};
    const auto nc = td["needsComment"];
    if (nc && nc.type() == bsoncxx::type::k_bool && nc.get_bool().value)
      a.insert("needsComment", true);
    const auto rf = td["requiredFields"];
    if (rf && rf.type() == bsoncxx::type::k_array) {
      QJsonArray arr;
      for (const auto &f : rf.get_array().value)
        arr.append(QString::fromStdString(std::string(f.get_string().value)));
      if (!arr.isEmpty())
        a.insert("requiredFields", arr);
    }
    out.append(a);
  }
  return out;
}

// Ядро перехода: проверки + запись состояния + событие в request_events.
ApplyResult applyTransition(App &app, const bsoncxx::document::view &tmpl,
                            const bsoncxx::document::view &req, const QString &action,
                            const QJsonObject &payload, const QString &comment,
                            const Actor &actor) {
  ApplyResult r;
  const std::string cur = strField(req, "state");
  if (stateTerminal(tmpl, cur)) {
    r.http = 400;
    r.code = "terminal";
    r.msg = "request is archived";
    return r;
  }
  auto tr = findTransition(tmpl, cur, action);
  if (!tr) {
    r.http = 400;
    r.code = "bad_transition";
    r.msg = "action not allowed from current state";
    return r;
  }
  const auto td = *tr;
  const std::string to = strField(td, "to");

  // Роль / гостевой scope.
  bool allowed = false;
  if (actor.type == "guest") {
    const auto ad = td["action"].get_document().value;
    const std::string akey = strField(ad, "key");
    for (const auto &s : actor.guestScope)
      if (s == akey) {
        allowed = true;
        break;
      }
  } else {
    const auto rl = td["roles"];
    if (rl && rl.type() == bsoncxx::type::k_array)
      for (const auto &x : rl.get_array().value) {
        const std::string rs(x.get_string().value);
        for (const auto &ur : actor.roles)
          if (ur == rs) {
            allowed = true;
            break;
          }
        if (allowed)
          break;
      }
  }
  if (!allowed) {
    r.http = 403;
    r.code = "forbidden";
    r.msg = "role not allowed for this transition";
    return r;
  }

  // requiredFields в payload.
  const auto rf = td["requiredFields"];
  if (rf && rf.type() == bsoncxx::type::k_array)
    for (const auto &f : rf.get_array().value) {
      const std::string fs(f.get_string().value);
      if (!payload.contains(QString::fromStdString(fs)) ||
          payload.value(QString::fromStdString(fs)).isNull()) {
        r.http = 400;
        r.code = "missing_field";
        r.msg = QString("field required: %1").arg(QString::fromStdString(fs));
        return r;
      }
    }

  // Гвард разделения обязанностей: исполнитель отличен от автора указанного
  // действия, если нет proxy с основанием.
  const auto gd = td["guards"];
  if (gd && gd.type() == bsoncxx::type::k_document) {
    const auto gdv = gd.get_document().value;
    const std::string sep = strField(gdv, "separationFrom");
    if (!sep.empty() && actor.type == "user") {
      const bool proxyOk =
          strField(gdv, "proxyAllowed") == "true" || [&] {
            const auto pa = gdv["proxyAllowed"];
            return pa && pa.type() == bsoncxx::type::k_bool && pa.get_bool().value;
          }();
      if (!(proxyOk && !actor.onBehalfOf.empty())) {
        try {
          auto ev = app.db["request_events"].find_one(b::make_document(
              kvp("requestId", req["_id"].get_oid().value),
              kvp("action", sep)));
          if (ev) {
            const auto av = (*ev).view()["actor"].get_document().value;
            if (av["userId"] && av["userId"].type() == bsoncxx::type::k_oid &&
                av["userId"].get_oid().value.to_string() == actor.userId) {
              r.http = 403;
              r.code = "separation";
              r.msg = "author of previous step cannot approve (use proxy with evidence)";
              return r;
            }
          }
        } catch (...) {
        }
      }
    }
  }

  // Лимит reopen.
  const auto cro = td["countAsReopen"];
  const bool isReopen =
      cro && cro.type() == bsoncxx::type::k_bool && cro.get_bool().value;
  int reopenCount = 0;
  {
    const auto rc = req["reopenCount"];
    if (rc && rc.type() == bsoncxx::type::k_int32)
      reopenCount = rc.get_int32().value;
  }
  if (isReopen) {
    int limit = 2;
    const auto rl2 = tmpl["reopenLimit"];
    if (rl2 && rl2.type() == bsoncxx::type::k_int32)
      limit = rl2.get_int32().value;
    if (reopenCount >= limit) {
      r.http = 400;
      r.code = "reopen_limit";
      r.msg = "reopen limit reached";
      return r;
    }
  }

  // Применяем: state + поля payload + assignees + reopenCount.
  try {
    auto setDoc = b::document{};
    setDoc.append(kvp("state", to));
    setDoc.append(kvp("updatedAt", bsoncxx::types::b_date(Clock::now())));
    const QJsonObject fields = payload.value("fields").toObject();
    const QJsonObject src = fields.isEmpty() ? payload : fields;
    static const char *reserved[] = {"state", "tenantId", "requesterId", "createdAt",
                                     "templateKey", "templateVersion", "reopenCount", nullptr};
    for (auto it = src.begin(); it != src.end(); ++it) {
      bool skip = false;
      for (int i = 0; reserved[i]; ++i)
        if (it.key() == reserved[i]) {
          skip = true;
          break;
        }
      if (!skip && it.key() != "assignees" && it.key() != "comment" && it.key() != "action")
        setDoc.append(kvp("fields." + it.key().toStdString(), j2b(it.value())));
    }
    // assignees — только привилегированные роли.
    const QJsonValue av = payload.value("assignees").isUndefined()
                              ? src.value("assignees")
                              : payload.value("assignees");
    if (av.isArray() && actor.type == "user") {
      bool priv = false;
      for (const auto &role : actor.roles)
        if (role == "dispatcher" || role == "approver" || role == "tenant_admin")
          priv = true;
      if (priv) {
        b::array arr;
        for (const auto &u : av.toArray()) {
          bsoncxx::oid oid;
          if (parseOid(u.toString(), oid))
            arr.append(oid);
        }
        setDoc.append(kvp("assigneeIds", arr.extract()));
      }
    }
    b::document update;
    update.append(kvp("$set", setDoc.extract()));
    if (isReopen)
      update.append(kvp("$inc", b::make_document(kvp("reopenCount", (int32_t)1))));
    app.db["requests"].update_one(
        b::make_document(kvp("_id", req["_id"].get_oid().value)), update.extract());

    // Событие в request_events (append-only).
    b::document ev;
    ev.append(kvp("tenantId", req["tenantId"].get_oid().value));
    ev.append(kvp("requestId", req["_id"].get_oid().value));
    ev.append(kvp("ts", bsoncxx::types::b_date(Clock::now())));
    b::document actorDoc;
    actorDoc.append(kvp("type", actor.type.toStdString()));
    actorDoc.append(kvp("label", actor.label));
    if (actor.type == "user")
      actorDoc.append(kvp("userId", bsoncxx::oid{actor.userId}));
    ev.append(kvp("actor", actorDoc.extract()));
    ev.append(kvp("from", cur));
    ev.append(kvp("to", to));
    ev.append(kvp("action", action.toStdString()));
    if (!comment.isEmpty())
      ev.append(kvp("comment", comment.toStdString()));
    if (!actor.onBehalfOf.empty())
      ev.append(kvp("onBehalfOf", actor.onBehalfOf));
    app.db["request_events"].insert_one(ev.extract());

    r.ok = true;
    r.http = 200;
    r.newState = to;
    return r;
  } catch (const std::exception &) {
    r.http = 500;
    r.code = "internal";
    r.msg = "transition failed";
    return r;
  }
}

int limitOf(const QHttpServerRequest &req);
int limitOf(const QHttpServerRequest &req) {
  bool ok = false;
  const int l = QUrlQuery(req.url()).queryItemValue("limit").toInt(&ok);
  return (ok && l > 0) ? std::min(l, 100) : 20;
}

std::optional<Principal> needAuth(App &app, const QHttpServerRequest &req,
                                  QHttpServerResponse &deny);
std::optional<Principal> needAuth(App &app, const QHttpServerRequest &req,
                                  QHttpServerResponse &deny) {
  auto p = principalFor(app, req);
  if (!p)
    deny = errResp("unauthorized", "invalid or expired token",
                   QHttpServerResponse::StatusCode::Unauthorized);
  return p;
}

} // namespace

void registerEngine(QHttpServer &s, App &app) {
  // Опубликованный шаблон для отрисовки пайплайна: ?kind=service_request.
  s.route("/api/v1/workflows", QHttpServerRequest::Method::Get,
          [&app](const QHttpServerRequest &req) {
            QHttpServerResponse deny(
                QHttpServerResponse::StatusCode::Unauthorized);
            auto p = needAuth(app, req, deny);
            if (!p)
              return deny;
            const QString kind = QUrlQuery(req.url()).queryItemValue("kind");
            if (kind.isEmpty())
              return errResp("bad_request", "?kind= required",
                             QHttpServerResponse::StatusCode::BadRequest);
            try {
              auto t = loadFlow(app, bsoncxx::oid{p->tenantId}, kind.toStdString());
              if (!t)
                return errResp("not_found", "no published template",
                               QHttpServerResponse::StatusCode::NotFound);
              return jsonResp(docToJson(t->view()));
            } catch (...) {
              return errResp("internal", "load failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  // Создание заявки: сразу submit (draft->triage) одним вызовом.
  s.route("/api/v1/requests", QHttpServerRequest::Method::Post,
          [&app](const QHttpServerRequest &req) {
            QHttpServerResponse deny(
                QHttpServerResponse::StatusCode::Unauthorized);
            auto p = needAuth(app, req, deny);
            if (!p)
              return deny;
            bool ok = false;
            const QJsonObject body = reqJson(req, ok);
            if (!ok)
              return errResp("bad_request", "invalid JSON",
                             QHttpServerResponse::StatusCode::BadRequest);
            const std::string tkey =
                body.value("templateKey").toString().toStdString();
            if (tkey.empty())
              return errResp("bad_request", "templateKey required",
                             QHttpServerResponse::StatusCode::BadRequest);
            try {
              const bsoncxx::oid tid{p->tenantId};
              auto tmpl = loadFlow(app, tid, tkey);
              if (!tmpl)
                return errResp("not_found", "no published template",
                               QHttpServerResponse::StatusCode::NotFound);
              const auto tv = tmpl->view();
              // Первая стадия из шаблона.
              std::string initial;
              const auto sts = tv["states"];
              if (sts && sts.type() == bsoncxx::type::k_array)
                for (const auto &st : sts.get_array().value) {
                  initial = strField(st.get_document().value, "key");
                  break;
                }
              if (initial.empty())
                return errResp("internal", "template has no states",
                               QHttpServerResponse::StatusCode::InternalServerError);
              const QJsonObject fields = body.value("fields").toObject();
              for (const char *f : {"title", "category", "priority", "description"})
                if (!fields.contains(f) || fields.value(f).toString().isEmpty())
                  return errResp("bad_request",
                                 QString("field required: %1").arg(f),
                                 QHttpServerResponse::StatusCode::BadRequest);
              const QString prio = fields.value("priority").toString();
              if (prio != "low" && prio != "normal" && prio != "high" &&
                  prio != "urgent")
                return errResp("bad_request", "priority: low|normal|high|urgent",
                               QHttpServerResponse::StatusCode::BadRequest);
              // dueAt из slaByPriority.
              auto now = Clock::now();
              auto due = now + std::chrono::hours(24);
              const auto sla = tv["slaByPriority"];
              if (sla && sla.type() == bsoncxx::type::k_document) {
                const auto sdv = sla.get_document().value;
                const auto h = sdv[prio.toStdString()];
                if (h && h.type() == bsoncxx::type::k_int32)
                  due = now + std::chrono::hours(h.get_int32().value);
              }
              auto ins = app.db["requests"].insert_one(b::make_document(
                  kvp("tenantId", tid), kvp("templateKey", tkey),
                  kvp("templateVersion", tv["version"].get_int32().value),
                  kvp("state", initial), kvp("fields", j2b(fields)),
                  kvp("requesterId", bsoncxx::oid{p->userId}),
                  kvp("assigneeIds", b::make_array()),
                  kvp("approvalMode", "proxy"),
                  kvp("priority", prio.toStdString()),
                  kvp("branchId", body.value("branchId").toString().toStdString()),
                  kvp("counterpartyId", body.value("counterpartyId").toString().toStdString()),
                  kvp("dueAt", bsoncxx::types::b_date(due)),
                  kvp("reopenCount", (int32_t)0),
                  kvp("createdBy", bsoncxx::oid{p->userId}),
                  kvp("createdAt", bsoncxx::types::b_date(now)),
                  kvp("updatedAt", bsoncxx::types::b_date(now))));
              auto got = app.db["requests"].find_one(b::make_document(
                  kvp("_id", ins->inserted_id().get_oid().value)));
              // Авто-submit draft->triage если автору можно.
              Actor actor;
              actor.type = "user";
              actor.userId = p->userId;
              actor.label = p->login;
              actor.roles = p->roles;
              auto applied = applyTransition(app, tv, got->view(), "submit", {},
                                             {}, actor);
              auto cur = app.db["requests"].find_one(b::make_document(
                  kvp("_id", ins->inserted_id().get_oid().value)));
              QJsonObject resp = docToJson(cur->view());
              (void)applied; // submit может быть недоступен роли — тогда остаемся в draft
              return jsonResp(resp);
            } catch (const std::exception &) {
              return errResp("internal", "create failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  // Список заявок с RBAC-фильтром.
  s.route("/api/v1/requests", QHttpServerRequest::Method::Get,
          [&app](const QHttpServerRequest &req) {
            QHttpServerResponse deny(
                QHttpServerResponse::StatusCode::Unauthorized);
            auto p = needAuth(app, req, deny);
            if (!p)
              return deny;
            try {
              const bsoncxx::oid tid{p->tenantId};
              const bsoncxx::oid me{p->userId};
              b::document filter;
              filter.append(kvp("tenantId", tid));
              const QUrlQuery q(req.url());
              if (!q.queryItemValue("state").isEmpty())
                filter.append(
                    kvp("state", q.queryItemValue("state").toStdString()));
              if (!q.queryItemValue("priority").isEmpty())
                filter.append(
                    kvp("priority", q.queryItemValue("priority").toStdString()));
              if (q.queryItemValue("overdue") == "1")
                filter.append(kvp("dueAt", b::make_document(kvp(
                                                "$lt", bsoncxx::types::b_date(Clock::now())))));
              bool privileged = hasRole(*p, "tenant_admin") || hasRole(*p, "dispatcher");
              if (!privileged) {
                b::array orArr;
                orArr.append(b::make_document(kvp("requesterId", me)));
                orArr.append(b::make_document(kvp("assigneeIds", me)));
                filter.append(kvp("$or", orArr.extract()));
              } else if (q.queryItemValue("mine") == "1") {
                filter.append(kvp("requesterId", me));
              }
              mongocxx::options::find opts;
              opts.limit(limitOf(req));
              opts.sort(b::make_document(kvp("updatedAt", -1)));
              QJsonArray items;
              for (const auto &d : app.db["requests"].find(filter.extract(), opts)) {
                QJsonObject o = docToJson(d);
                const auto due = d["dueAt"];
                const bool term = [&] {
                  auto t = loadFlow(app, tid, strField(d, "templateKey"));
                  return t && stateTerminal(t->view(), strField(d, "state"));
                }();
                o.insert("overdue",
                         due && due.type() == bsoncxx::type::k_date &&
                             Clock::time_point(due.get_date().value) < Clock::now() && !term);
                items.append(o);
              }
              return jsonResp({{"items", items}});
            } catch (...) {
              return errResp("internal", "list failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  // Карточка + allowedActions + история.
  s.route("/api/v1/requests/<arg>", QHttpServerRequest::Method::Get,
          [&app](const QString &id, const QHttpServerRequest &req) {
            QHttpServerResponse deny(
                QHttpServerResponse::StatusCode::Unauthorized);
            auto p = needAuth(app, req, deny);
            if (!p)
              return deny;
            try {
              bsoncxx::oid rid;
              if (!parseOid(id, rid))
                return errResp("bad_request", "bad id",
                               QHttpServerResponse::StatusCode::BadRequest);
              auto got = app.db["requests"].find_one(b::make_document(
                  kvp("_id", rid), kvp("tenantId", bsoncxx::oid{p->tenantId})));
              if (!got)
                return errResp("not_found", "no such request",
                               QHttpServerResponse::StatusCode::NotFound);
              const auto rv = got->view();
              const bool vis =
                  hasRole(*p, "tenant_admin") || hasRole(*p, "dispatcher") ||
                  strField(rv, "requesterId") == "" || [&] {
                    const auto rq = rv["requesterId"];
                    return rq && rq.type() == bsoncxx::type::k_oid &&
                           rq.get_oid().value.to_string() == p->userId;
                  }() ||
                  [&] {
                    const auto as = rv["assigneeIds"];
                    if (!as || as.type() != bsoncxx::type::k_array)
                      return false;
                    for (const auto &a : as.get_array().value)
                      if (a.get_oid().value.to_string() == p->userId)
                        return true;
                    return false;
                  }();
              if (!vis)
                return errResp("forbidden", "not your request",
                               QHttpServerResponse::StatusCode::Forbidden);
              auto tmpl = loadFlow(app, bsoncxx::oid{p->tenantId},
                                   strField(rv, "templateKey"));
              QJsonObject resp = docToJson(rv);
              if (tmpl) {
                Actor actor;
                actor.type = "user";
                actor.userId = p->userId;
                actor.label = p->login;
                actor.roles = p->roles;
                resp.insert("allowedActions",
                            allowedActions(tmpl->view(), strField(rv, "state"), actor));
                resp.insert("stateTitle",
                            stateTitle(tmpl->view(), strField(rv, "state")));
              }
              QJsonArray hist;
              mongocxx::options::find hopts;
              hopts.sort(b::make_document(kvp("ts", 1)));
              hopts.limit(100);
              for (const auto &e : app.db["request_events"].find(
                       b::make_document(kvp("requestId", rid)), hopts))
                hist.append(docToJson(e));
              resp.insert("history", hist);
              return jsonResp(resp);
            } catch (...) {
              return errResp("internal", "detail failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  // Единственная точка смены стадий.
  s.route("/api/v1/requests/<arg>/transitions", QHttpServerRequest::Method::Post,
          [&app](const QString &id, const QHttpServerRequest &req) {
            QHttpServerResponse deny(
                QHttpServerResponse::StatusCode::Unauthorized);
            auto p = needAuth(app, req, deny);
            if (!p)
              return deny;
            bool ok = false;
            const QJsonObject body = reqJson(req, ok);
            if (!ok)
              return errResp("bad_request", "invalid JSON",
                             QHttpServerResponse::StatusCode::BadRequest);
            try {
              bsoncxx::oid rid;
              if (!parseOid(id, rid))
                return errResp("bad_request", "bad id",
                               QHttpServerResponse::StatusCode::BadRequest);
              auto got = app.db["requests"].find_one(b::make_document(
                  kvp("_id", rid), kvp("tenantId", bsoncxx::oid{p->tenantId})));
              if (!got)
                return errResp("not_found", "no such request",
                               QHttpServerResponse::StatusCode::NotFound);
              auto tmpl = loadFlow(app, bsoncxx::oid{p->tenantId},
                                   strField(got->view(), "templateKey"));
              if (!tmpl)
                return errResp("internal", "template missing",
                               QHttpServerResponse::StatusCode::InternalServerError);
              Actor actor;
              actor.type = "user";
              actor.userId = p->userId;
              actor.label = p->login;
              actor.roles = p->roles;
              actor.onBehalfOf = body.value("onBehalfOf").toString().toStdString();
              auto r = applyTransition(app, tmpl->view(), got->view(),
                                       body.value("action").toString(), body,
                                       body.value("comment").toString(), actor);
              if (!r.ok)
                return errResp(r.code, r.msg,
                               (QHttpServerResponse::StatusCode)r.http);
              auto cur = app.db["requests"].find_one(b::make_document(
                  kvp("_id", rid)));
              QJsonObject resp = docToJson(cur->view());
              resp.insert("allowedActions",
                          allowedActions(tmpl->view(), r.newState, actor));
              return jsonResp(resp);
            } catch (...) {
              return errResp("internal", "transition failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  // Режим согласования proxy|guest + выдача ссылки.
  s.route("/api/v1/requests/<arg>/mode", QHttpServerRequest::Method::Post,
          [&app](const QString &id, const QHttpServerRequest &req) {
            QHttpServerResponse deny(
                QHttpServerResponse::StatusCode::Unauthorized);
            auto p = needAuth(app, req, deny);
            if (!p)
              return deny;
            if (!hasRole(*p, "dispatcher") && !hasRole(*p, "approver") &&
                !hasRole(*p, "tenant_admin"))
              return errResp("forbidden", "dispatcher/approver required",
                             QHttpServerResponse::StatusCode::Forbidden);
            bool ok = false;
            const QJsonObject body = reqJson(req, ok);
            if (!ok)
              return errResp("bad_request", "invalid JSON",
                             QHttpServerResponse::StatusCode::BadRequest);
            const QString mode = body.value("approvalMode").toString();
            if (mode != "proxy" && mode != "guest")
              return errResp("bad_request", "approvalMode: proxy|guest",
                             QHttpServerResponse::StatusCode::BadRequest);
            try {
              bsoncxx::oid rid;
              if (!parseOid(id, rid))
                return errResp("bad_request", "bad id",
                               QHttpServerResponse::StatusCode::BadRequest);
              const bsoncxx::oid tid{p->tenantId};
              auto got = app.db["requests"].find_one(
                  b::make_document(kvp("_id", rid), kvp("tenantId", tid)));
              if (!got)
                return errResp("not_found", "no such request",
                               QHttpServerResponse::StatusCode::NotFound);
              app.db["requests"].update_one(
                  b::make_document(kvp("_id", rid)),
                  b::make_document(kvp("$set", b::make_document(
                                                        kvp("approvalMode", mode.toStdString())))));
              QJsonObject resp{{"approvalMode", mode}};
              if (mode == "guest") {
                // scope = действия шаблона с guestScope.
                auto tmpl = loadFlow(app, tid, strField(got->view(), "templateKey"));
                b::array scope;
                if (tmpl) {
                  const auto trs = tmpl->view()["transitions"];
                  if (trs && trs.type() == bsoncxx::type::k_array)
                    for (const auto &t : trs.get_array().value) {
                      const auto gs = t.get_document().value["guestScope"];
                      if (gs && gs.type() == bsoncxx::type::k_array)
                        for (const auto &a : gs.get_array().value)
                          scope.append(
                              std::string(a.get_string().value));
                    }
                }
                const std::string token = randHex(32);
                auto now = Clock::now();
                int ttlH = 72;
                if (tmpl) {
                  const auto gh = tmpl->view()["guestTtlHours"];
                  if (gh && gh.type() == bsoncxx::type::k_int32)
                    ttlH = gh.get_int32().value;
                }
                app.db["guest_tokens"].insert_one(b::make_document(
                    kvp("tenantId", tid), kvp("requestId", rid),
                    kvp("scope", scope.extract()),
                    kvp("tokenHash",
                        std::string(sha256Hex(QByteArray::fromStdString(token))
                                        .toLatin1()
                                        .constData())),
                    kvp("expiresAt", bsoncxx::types::b_date(
                                         now + std::chrono::hours(ttlH))),
                    kvp("usedActions", b::make_array()),
                    kvp("createdBy", bsoncxx::oid{p->userId}),
                    kvp("createdAt", bsoncxx::types::b_date(now))));
                resp.insert("token", QString::fromStdString(token));
                resp.insert("link", QString("/g/%1").arg(QString::fromStdString(token)));
              }
              return jsonResp(resp);
            } catch (...) {
              return errResp("internal", "mode failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  // Комментарии.
  s.route("/api/v1/requests/<arg>/comments", QHttpServerRequest::Method::Post,
          [&app](const QString &id, const QHttpServerRequest &req) {
            QHttpServerResponse deny(
                QHttpServerResponse::StatusCode::Unauthorized);
            auto p = needAuth(app, req, deny);
            if (!p)
              return deny;
            bool ok = false;
            const QJsonObject body = reqJson(req, ok);
            const QString text = body.value("text").toString().trimmed();
            if (!ok || text.isEmpty())
              return errResp("bad_request", "text required",
                             QHttpServerResponse::StatusCode::BadRequest);
            try {
              bsoncxx::oid rid;
              if (!parseOid(id, rid))
                return errResp("bad_request", "bad id",
                               QHttpServerResponse::StatusCode::BadRequest);
              auto got = app.db["requests"].find_one(b::make_document(
                  kvp("_id", rid), kvp("tenantId", bsoncxx::oid{p->tenantId})));
              if (!got)
                return errResp("not_found", "no such request",
                               QHttpServerResponse::StatusCode::NotFound);
              auto ins = app.db["request_comments"].insert_one(b::make_document(
                  kvp("tenantId", bsoncxx::oid{p->tenantId}), kvp("requestId", rid),
                  kvp("authorId", bsoncxx::oid{p->userId}),
                  kvp("authorType", "user"),
                  kvp("authorLabel", p->login),
                  kvp("text", text.toStdString()),
                  kvp("createdAt", bsoncxx::types::b_date(Clock::now()))));
              auto c = app.db["request_comments"].find_one(b::make_document(
                  kvp("_id", ins->inserted_id().get_oid().value)));
              return jsonResp(docToJson(c->view()));
            } catch (...) {
              return errResp("internal", "comment failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  // --- гости: карточка и переходы по токену ---
  auto guestToken = [&app](const QString &token)
      -> std::optional<bsoncxx::document::value> {
    try {
      const QString h = sha256Hex(token.toUtf8());
      auto t = app.db["guest_tokens"].find_one(b::make_document(
          kvp("tokenHash", std::string(h.toLatin1().constData()))));
      if (!t)
        return std::nullopt;
      const auto tv = t->view();
      if (Clock::time_point(tv["expiresAt"].get_date().value) < Clock::now())
        return std::nullopt;
      return bsoncxx::document::value(*t);
    } catch (...) {
      return std::nullopt;
    }
  };

  s.route("/g/<arg>", QHttpServerRequest::Method::Get,
          [&app, guestToken](const QString &token) {
            auto t = guestToken(token);
            if (!t)
              return errResp("bad_token", "unknown or expired link",
                             QHttpServerResponse::StatusCode::NotFound);
            try {
              const auto tv = t->view();
              const bsoncxx::oid tid = tv["tenantId"].get_oid().value;
              const bsoncxx::oid rid = tv["requestId"].get_oid().value;
              auto got = app.db["requests"].find_one(
                  b::make_document(kvp("_id", rid), kvp("tenantId", tid)));
              if (!got)
                return errResp("not_found", "request gone",
                               QHttpServerResponse::StatusCode::NotFound);
              auto tmpl = loadFlow(app, tid, strField(got->view(), "templateKey"));
              if (!tmpl)
                return errResp("internal", "template missing",
                               QHttpServerResponse::StatusCode::InternalServerError);
              Actor actor;
              actor.type = "guest";
              actor.label = "Guest";
              const auto sc = tv["scope"];
              if (sc && sc.type() == bsoncxx::type::k_array)
                for (const auto &a : sc.get_array().value)
                  actor.guestScope.emplace_back(std::string(a.get_string().value));
              const auto used = tv["usedActions"];
              QJsonArray acts =
                  allowedActions(tmpl->view(), strField(got->view(), "state"), actor);
              QJsonArray fresh;
              for (const auto &a : acts) {
                const QString ak = a.toObject().value("action").toString();
                bool done = false;
                if (used && used.type() == bsoncxx::type::k_array)
                  for (const auto &u : used.get_array().value)
                    if (std::string(u.get_string().value) == ak.toStdString())
                      done = true;
                if (!done)
                  fresh.append(a);
              }
              const auto rv = got->view();
              QJsonObject card{
                  {"title", QString::fromStdString(strField(
                                        rv["fields"].get_document().value, "title"))},
                  {"state", QString::fromStdString(strField(rv, "state"))},
                  {"stateTitle",
                   stateTitle(tmpl->view(), strField(rv, "state"))},
                  {"allowedActions", fresh}};
              return jsonResp(card);
            } catch (...) {
              return errResp("internal", "guest card failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  s.route("/g/<arg>/transitions", QHttpServerRequest::Method::Post,
          [&app, guestToken](const QString &token, const QHttpServerRequest &req) {
            auto t = guestToken(token);
            if (!t)
              return errResp("bad_token", "unknown or expired link",
                             QHttpServerResponse::StatusCode::NotFound);
            bool ok = false;
            const QJsonObject body = reqJson(req, ok);
            if (!ok)
              return errResp("bad_request", "invalid JSON",
                             QHttpServerResponse::StatusCode::BadRequest);
            try {
              const auto tv = t->view();
              const bsoncxx::oid tid = tv["tenantId"].get_oid().value;
              const bsoncxx::oid rid = tv["requestId"].get_oid().value;
              const QString action = body.value("action").toString();
              // одноразовость действия на токен.
              const auto used = tv["usedActions"];
              if (used && used.type() == bsoncxx::type::k_array)
                for (const auto &u : used.get_array().value)
                  if (std::string(u.get_string().value) == action.toStdString())
                    return errResp("used", "action already used",
                                   QHttpServerResponse::StatusCode::Conflict);
              auto got = app.db["requests"].find_one(
                  b::make_document(kvp("_id", rid), kvp("tenantId", tid)));
              if (!got)
                return errResp("not_found", "request gone",
                               QHttpServerResponse::StatusCode::NotFound);
              auto tmpl = loadFlow(app, tid, strField(got->view(), "templateKey"));
              if (!tmpl)
                return errResp("internal", "template missing",
                               QHttpServerResponse::StatusCode::InternalServerError);
              Actor actor;
              actor.type = "guest";
              actor.label = "Guest";
              const auto sc = tv["scope"];
              if (sc && sc.type() == bsoncxx::type::k_array)
                for (const auto &a : sc.get_array().value)
                  actor.guestScope.emplace_back(std::string(a.get_string().value));
              auto r = applyTransition(app, tmpl->view(), got->view(), action, body,
                                       body.value("comment").toString(), actor);
              if (!r.ok)
                return errResp(r.code, r.msg,
                               (QHttpServerResponse::StatusCode)r.http);
              app.db["guest_tokens"].update_one(
                  b::make_document(kvp("_id", tv["_id"].get_oid().value)),
                  b::make_document(kvp("$push", b::make_document(
                                                         kvp("usedActions", action.toStdString())))));
              return jsonResp({{"ok", true}, {"state", QString::fromStdString(r.newState)}});
            } catch (...) {
              return errResp("internal", "guest transition failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  // --- документы-минимум: create/list/detail, номера из counters ---
  s.route("/api/v1/documents", QHttpServerRequest::Method::Post,
          [&app](const QHttpServerRequest &req) {
            QHttpServerResponse deny(
                QHttpServerResponse::StatusCode::Unauthorized);
            auto p = needAuth(app, req, deny);
            if (!p)
              return deny;
            bool ok = false;
            const QJsonObject body = reqJson(req, ok);
            if (!ok)
              return errResp("bad_request", "invalid JSON",
                             QHttpServerResponse::StatusCode::BadRequest);
            try {
              const bsoncxx::oid tid{p->tenantId};
              const std::string tkey =
                  body.value("templateKey").toString().toStdString();
              // актуальный опубликованный doc-шаблон.
              mongocxx::options::find fopts;
              fopts.sort(b::make_document(kvp("version", -1)));
              fopts.limit(1);
              auto tcur = app.db["doc_templates"].find(
                  b::make_document(kvp("tenantId", tid), kvp("key", tkey),
                                   kvp("status", "published")),
                  fopts);
              auto tit = tcur.begin();
              if (tit == tcur.end())
                return errResp("not_found", "no published template",
                               QHttpServerResponse::StatusCode::NotFound);
              const auto tv = *tit;
              const QJsonObject fields = body.value("fields").toObject();
              const auto fl = tv["fields"];
              if (fl && fl.type() == bsoncxx::type::k_array)
                for (const auto &f : fl.get_array().value) {
                  const auto fd = f.get_document().value;
                  const std::string fk = strField(fd, "key");
                  const auto rq = fd["required"];
                  if (rq && rq.type() == bsoncxx::type::k_bool && rq.get_bool().value &&
                      (!fields.contains(QString::fromStdString(fk)) ||
                       fields.value(QString::fromStdString(fk)).isNull()))
                    return errResp("bad_request",
                                   QString("field required: %1")
                                       .arg(QString::fromStdString(fk)),
                                   QHttpServerResponse::StatusCode::BadRequest);
                }
              // Номер: counters {_id:{tenantId,key,period=year}}.
              const std::string period = "y" + std::to_string(
                  std::chrono::duration_cast<std::chrono::hours>(
                      Clock::now().time_since_epoch()).count() / 8760);
              mongocxx::options::find_one_and_update upopts;
              upopts.upsert(true);
              upopts.return_document(mongocxx::options::return_document::k_after);
              auto cnt = app.db["counters"].find_one_and_update(
                  b::make_document(
                      kvp("tenantId", tid), kvp("key", tkey), kvp("period", period)),
                  b::make_document(kvp("$inc", b::make_document(kvp("seq", (int32_t)1)))),
                  upopts);
              int32_t seq = 1;
              if (cnt && (*cnt).view()["seq"].type() == bsoncxx::type::k_int32)
                seq = (*cnt).view()["seq"].get_int32().value;
              auto now = Clock::now();
              auto ins = app.db["documents"].insert_one(b::make_document(
                  kvp("tenantId", tid), kvp("templateKey", tkey),
                  kvp("templateVersion", tv["version"].get_int32().value),
                  kvp("number", tkey + "-" + period + "-" + std::to_string(seq)),
                  kvp("fields", j2b(fields)),
                  kvp("branchId", body.value("branchId").toString().toStdString()),
                  kvp("status", "active"), kvp("createdBy", bsoncxx::oid{p->userId}),
                  kvp("createdAt", bsoncxx::types::b_date(now)),
                  kvp("updatedAt", bsoncxx::types::b_date(now))));
              auto got = app.db["documents"].find_one(b::make_document(
                  kvp("_id", ins->inserted_id().get_oid().value)));
              return jsonResp(docToJson(got->view()));
            } catch (const std::exception &) {
              return errResp("internal", "create failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  s.route("/api/v1/documents", QHttpServerRequest::Method::Get,
          [&app](const QHttpServerRequest &req) {
            QHttpServerResponse deny(
                QHttpServerResponse::StatusCode::Unauthorized);
            auto p = needAuth(app, req, deny);
            if (!p)
              return deny;
            try {
              b::document filter;
              filter.append(kvp("tenantId", bsoncxx::oid{p->tenantId}));
              const QUrlQuery q(req.url());
              if (!q.queryItemValue("templateKey").isEmpty())
                filter.append(kvp("templateKey",
                                  q.queryItemValue("templateKey").toStdString()));
              mongocxx::options::find opts;
              opts.limit(limitOf(req));
              opts.sort(b::make_document(kvp("updatedAt", -1)));
              QJsonArray items;
              for (const auto &d : app.db["documents"].find(filter.extract(), opts))
                items.append(docToJson(d));
              return jsonResp({{"items", items}});
            } catch (...) {
              return errResp("internal", "list failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });

  s.route("/api/v1/documents/<arg>", QHttpServerRequest::Method::Get,
          [&app](const QString &id, const QHttpServerRequest &req) {
            QHttpServerResponse deny(
                QHttpServerResponse::StatusCode::Unauthorized);
            auto p = needAuth(app, req, deny);
            if (!p)
              return deny;
            try {
              bsoncxx::oid did;
              if (!parseOid(id, did))
                return errResp("bad_request", "bad id",
                               QHttpServerResponse::StatusCode::BadRequest);
              auto got = app.db["documents"].find_one(b::make_document(
                  kvp("_id", did), kvp("tenantId", bsoncxx::oid{p->tenantId})));
              if (!got)
                return errResp("not_found", "no such document",
                               QHttpServerResponse::StatusCode::NotFound);
              return jsonResp(docToJson(got->view()));
            } catch (...) {
              return errResp("internal", "detail failed",
                             QHttpServerResponse::StatusCode::InternalServerError);
            }
          });
}

void seedServiceRequest(App &app, const bsoncxx::oid &tid) {
  auto flows = app.db["workflow_templates"];
  if (flows.count_documents(b::make_document(kvp("tenantId", tid),
                                             kvp("key", "service_request"))) > 0)
    return;
  auto st = [](const char *k, const char *t, bool term = false) {
    return b::make_document(kvp("key", k), kvp("title", t), kvp("terminal", term));
  };
  auto act = [](const char *k, const char *t) {
    return b::make_document(kvp("key", k), kvp("title", t));
  };
  auto tr = [&](const char *from, const char *to, const char *ak, const char *at,
                std::vector<std::string> roles,
                std::vector<std::string> reqFields = {},
                std::vector<std::string> guest = {}, bool needsComment = false,
                bool countReopen = false, const char *sepFrom = nullptr) {
    b::document d;
    d.append(kvp("from", from), kvp("to", to), kvp("action", act(ak, at)));
    b::array rl;
    for (const auto &r : roles)
      rl.append(r);
    d.append(kvp("roles", rl.extract()));
    if (!reqFields.empty()) {
      b::array rf;
      for (const auto &f : reqFields)
        rf.append(f);
      d.append(kvp("requiredFields", rf.extract()));
    }
    if (!guest.empty()) {
      b::array gs;
      for (const auto &g : guest)
        gs.append(g);
      d.append(kvp("guestScope", gs.extract()));
    }
    if (needsComment)
      d.append(kvp("needsComment", true));
    if (countReopen)
      d.append(kvp("countAsReopen", true));
    if (sepFrom)
      d.append(kvp("guards", b::make_document(kvp("separationFrom", sepFrom),
                                              kvp("proxyAllowed", true))));
    return d.extract();
  };
  b::array states;
  states.append(st("draft", "Запрос"));
  states.append(st("triage", "Рассмотрение"));
  states.append(st("proposal", "Вариант решения"));
  states.append(st("approval", "Согласование"));
  states.append(st("execution", "Назначение/устранение"));
  states.append(st("confirmation", "Подтверждение"));
  states.append(st("archived", "Архив", true));
  b::array trs;
  trs.append(tr("draft", "triage", "submit", "Отправить", {"requester"}));
  trs.append(tr("triage", "proposal", "accept_triage", "Принять", {"dispatcher"}));
  trs.append(tr("triage", "archived", "reject_triage", "Отклонить", {"dispatcher"},
               {}, {}, true));
  trs.append(tr("triage", "draft", "return_triage", "Вернуть на доработку",
               {"dispatcher"}, {}, {}, true));
  trs.append(tr("proposal", "approval", "propose", "Предложить решение",
               {"analyst"}, {"solution", "estimate"}));
  trs.append(tr("approval", "execution", "approve", "Согласовать", {"approver"},
               {}, {"approve"}, false, false, "propose"));
  trs.append(tr("approval", "proposal", "reject_approval", "Отклонить",
               {"approver"}, {}, {}, true));
  trs.append(tr("execution", "execution", "assign", "Назначить",
               {"dispatcher", "tenant_admin"}));
  trs.append(tr("execution", "confirmation", "resolve", "Выполнено",
               {"specialist"}, {"report"}));
  trs.append(tr("confirmation", "archived", "confirm", "Подтвердить",
               {"requester"}, {}, {"confirm"}));
  trs.append(tr("confirmation", "execution", "reopen", "Вернуть в работу",
               {"requester"}, {}, {}, true, true));
  b::document sla;
  sla.append(kvp("low", (int32_t)72), kvp("normal", (int32_t)24),
             kvp("high", (int32_t)8), kvp("urgent", (int32_t)4));
  b::array modes;
  modes.append("proxy");
  modes.append("guest");
  flows.insert_one(b::make_document(
      kvp("tenantId", tid), kvp("key", "service_request"), kvp("version", (int32_t)1),
      kvp("status", "published"), kvp("title", "Сервисные обращения"),
      kvp("states", states.extract()), kvp("transitions", trs.extract()),
      kvp("slaByPriority", sla.extract()), kvp("approvalModes", modes.extract()),
      kvp("guestTtlHours", (int32_t)72), kvp("reopenLimit", (int32_t)2),
      kvp("instruction",
          "Proxy: согласование со слов/письма клиента с основанием. Guest: ссылка "
          "одноразовая на действие, просмотр до TTL."),
      kvp("createdAt", bsoncxx::types::b_date(Clock::now()))));
}

void ensureTenantDefaults(App &app, const bsoncxx::oid &tid) {
  auto roles = app.db["roles"];
  const char *baseRoles[][2] = {{"requester", "Заявитель"},
                                {"dispatcher", "Диспетчер"},
                                {"analyst", "Аналитик"},
                                {"approver", "Согласующий"},
                                {"specialist", "Специалист"}};
  for (const auto *r : baseRoles)
    if (roles.count_documents(b::make_document(kvp("tenantId", tid),
                                               kvp("roleKey", r[0]))) == 0)
      roles.insert_one(b::make_document(kvp("tenantId", tid), kvp("roleKey", r[0]),
                                        kvp("title", r[1])));
  seedServiceRequest(app, tid);
}

} // namespace kisa
