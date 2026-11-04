#include "dto.hpp"
#include <QJsonArray>

namespace kisa {

// Extended JSON b_date: {"$date":{"$numberLong":"ms"}} или {"$date": ms}
static QDateTime dateFromJson(const QJsonValue &v) {
    if (!v.isObject())
        return {};
    const QJsonObject d = v.toObject().value("$date").toObject();
    if (d.contains("$numberLong"))
        return QDateTime::fromMSecsSinceEpoch(d.value("$numberLong").toString().toLongLong());
    const double ms = v.toObject().value("$date").toDouble();
    if (ms != 0)
        return QDateTime::fromMSecsSinceEpoch((qint64)ms);
    return {};
}

AllowedAction AllowedAction::fromJson(const QJsonObject &o) {
    AllowedAction a;
    a.action = o.value("action").toString();
    a.title  = o.value("title").toString();
    a.needsComment = o.value("needsComment").toBool();
    for (const auto &f : o.value("requiredFields").toArray())
        a.requiredFields << f.toString();
    return a;
}

HistoryEvent HistoryEvent::fromJson(const QJsonObject &o) {
    HistoryEvent e;
    e.id      = o.value("_id").toString();
    e.from    = o.value("from").toString();
    e.to      = o.value("to").toString();
    e.action  = o.value("action").toString();
    e.comment = o.value("comment").toString();
    e.ts      = dateFromJson(o.value("ts"));
    const QJsonObject actor = o.value("actor").toObject();
    e.actorType  = actor.value("type").toString();
    e.actorLabel = actor.value("label").toString();
    return e;
}

ServiceRequest ServiceRequest::fromJson(const QJsonObject &o) {
    ServiceRequest r;
    r.id         = o.value("_id").toString();
    r.state      = o.value("state").toString();
    r.stateTitle = o.value("stateTitle").toString(r.state);
    r.fields     = o.value("fields").toObject();
    r.overdue    = o.value("overdue").toBool();
    r.dueAt      = dateFromJson(o.value("dueAt"));
    r.createdAt  = dateFromJson(o.value("createdAt"));
    r.updatedAt  = dateFromJson(o.value("updatedAt"));
    for (const auto &a : o.value("allowedActions").toArray())
        r.allowedActions << AllowedAction::fromJson(a.toObject());
    for (const auto &e : o.value("history").toArray())
        r.history << HistoryEvent::fromJson(e.toObject());
    return r;
}

UserProfile UserProfile::fromJson(const QJsonObject &o) {
    UserProfile p;
    p.login    = o.value("login").toString();
    p.name     = o.value("name").toString();
    p.tenantId = o.value("tenantId").toString();
    for (const auto &r : o.value("roles").toArray())
        p.roles << r.toString();
    return p;
}

} // namespace kisa
