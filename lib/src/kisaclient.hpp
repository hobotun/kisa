#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QUrlQuery>
#include "dto.hpp"
#include "tenantsession.hpp"

namespace kisa {

class KisaClient : public QObject {
    Q_OBJECT
public:
    explicit KisaClient(const QString &baseUrl, TenantSession *session,
                        QObject *parent = nullptr);

    void login(const QString &login, const QString &password,
               const QString &device = {});
    void me();
    void getRequests(const QUrlQuery &filter = {});
    void getRequest(const QString &id);
    void postTransition(const QString &id, const QString &action,
                        const QString &comment = {},
                        const QJsonObject &payload = {});
    void postComment(const QString &id, const QString &text);
    void logout();

signals:
    void loginDone(const UserProfile &user);
    void loginFailed(const QString &code, const QString &message);
    void meDone(const UserProfile &profile);
    void requestsDone(const QList<ServiceRequest> &items);
    void requestDone(const ServiceRequest &req);
    void transitionDone(const ServiceRequest &req);
    void networkError(const QString &message);

private:
    QNetworkAccessManager *m_nam;
    TenantSession *m_session;
    QString m_baseUrl;

    QNetworkRequest makeRequest(const QString &path) const;

    // GET helper → calls onSuccess(QJsonObject) or emits networkError
    void get(const QString &path,
             std::function<void(const QJsonObject &)> onSuccess);
    // POST helper
    void post(const QString &path, const QJsonObject &body,
              std::function<void(const QJsonObject &)> onSuccess,
              std::function<void(const QString &, const QString &)> onError = {});

    void handleRefreshAndRetry(QNetworkReply *reply,
                               std::function<void()> retry,
                               std::function<void()> onFail);
};

} // namespace kisa
