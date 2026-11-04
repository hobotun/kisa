#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace kisa {

struct AllowedAction {
    QString action;
    QString title;
    bool needsComment = false;
    QStringList requiredFields;

    static AllowedAction fromJson(const QJsonObject &o);
};

struct HistoryEvent {
    QString id;
    QString from;
    QString to;
    QString action;
    QString comment;
    QDateTime ts;
    QString actorType;  // "user" | "guest"
    QString actorLabel;

    static HistoryEvent fromJson(const QJsonObject &o);
};

struct ServiceRequest {
    QString id;
    QString state;
    QString stateTitle;
    QJsonObject fields;
    QList<AllowedAction> allowedActions;
    QList<HistoryEvent> history;
    bool overdue = false;
    QDateTime dueAt;
    QDateTime createdAt;
    QDateTime updatedAt;

    static ServiceRequest fromJson(const QJsonObject &o);

    QString title() const { return fields.value("title").toString(); }
    QString priority() const { return fields.value("priority").toString(); }
};

struct UserProfile {
    QString login;
    QString name;
    QStringList roles;
    QString tenantId;

    static UserProfile fromJson(const QJsonObject &o);
    bool hasRole(const QString &role) const { return roles.contains(role); }
    bool isPrivileged() const {
        return hasRole("tenant_admin") || hasRole("dispatcher");
    }
};

} // namespace kisa
