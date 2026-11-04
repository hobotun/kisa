#include "kisaclient.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSysInfo>

namespace kisa {

KisaClient::KisaClient(const QString &baseUrl, TenantSession *session, QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)),
      m_session(session), m_baseUrl(baseUrl) {}

QNetworkRequest KisaClient::makeRequest(const QString &path) const {
    QNetworkRequest req(QUrl(m_baseUrl + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_session->slug().isEmpty())
        req.setRawHeader("X-Tenant", m_session->slug().toUtf8());
    if (!m_session->accessToken().isEmpty())
        req.setRawHeader("Authorization",
                         ("Bearer " + m_session->accessToken()).toUtf8());
    return req;
}

static QJsonObject parseBody(QNetworkReply *reply) {
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(reply->readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object();
}

static QString errCode(const QJsonObject &o) {
    return o.value("error").toObject().value("code").toString("internal");
}
static QString errMsg(const QJsonObject &o) {
    return o.value("error").toObject().value("message").toString("unknown error");
}

void KisaClient::get(const QString &path,
                     std::function<void(const QJsonObject &)> onSuccess) {
    auto *reply = m_nam->get(makeRequest(path));
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, onSuccess]() mutable {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            onSuccess(parseBody(reply));
            return;
        }
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
            handleRefreshAndRetry(reply,
                [this, path, onSuccess]() { get(path, onSuccess); },
                [this]() { emit networkError("Session expired"); });
            return;
        }
        emit networkError(reply->errorString());
    });
}

void KisaClient::post(const QString &path, const QJsonObject &body,
                      std::function<void(const QJsonObject &)> onSuccess,
                      std::function<void(const QString &, const QString &)> onError) {
    auto *reply = m_nam->post(makeRequest(path), QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, body, onSuccess, onError]() mutable {
        reply->deleteLater();
        const QJsonObject obj = parseBody(reply);
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError) {
            onSuccess(obj);
            return;
        }
        if (status == 401) {
            handleRefreshAndRetry(reply,
                [this, path, body, onSuccess, onError]() { post(path, body, onSuccess, onError); },
                [this, onError]() {
                    if (onError) onError("unauthorized", "Session expired");
                    else emit networkError("Session expired");
                });
            return;
        }
        if (onError) onError(errCode(obj), errMsg(obj));
        else emit networkError(errMsg(obj));
    });
}

void KisaClient::handleRefreshAndRetry(QNetworkReply * /*reply*/,
                                       std::function<void()> retry,
                                       std::function<void()> onFail) {
    if (m_session->refreshToken().isEmpty()) {
        onFail();
        return;
    }
    QNetworkRequest req(QUrl(m_baseUrl + "/api/v1/auth/refresh"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QJsonObject body{{"refreshToken", m_session->refreshToken()}};
    auto *r = m_nam->post(req, QJsonDocument(body).toJson());
    connect(r, &QNetworkReply::finished, this, [this, r, retry, onFail]() {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            m_session->clear();
            onFail();
            return;
        }
        const QJsonObject obj = parseBody(r);
        m_session->setTokens(obj.value("accessToken").toString(),
                             obj.value("refreshToken").toString());
        retry();
    });
}

// --- public API ---

void KisaClient::login(const QString &login, const QString &password,
                       const QString &device) {
    const QJsonObject body{
        {"login", login},
        {"password", password},
        {"device", device.isEmpty() ? QSysInfo::prettyProductName() : device}};
    post("/api/v1/auth/login", body,
         [this](const QJsonObject &o) {
             m_session->setTokens(o.value("accessToken").toString(),
                                  o.value("refreshToken").toString());
             emit loginDone(UserProfile::fromJson(o.value("user").toObject()));
         },
         [this](const QString &code, const QString &msg) {
             emit loginFailed(code, msg);
         });
}

void KisaClient::me() {
    get("/api/v1/me", [this](const QJsonObject &o) {
        emit meDone(UserProfile::fromJson(o));
    });
}

void KisaClient::getRequests(const QUrlQuery &filter) {
    const QString path = filter.isEmpty()
                             ? "/api/v1/requests"
                             : "/api/v1/requests?" + filter.toString();
    get(path, [this](const QJsonObject &o) {
        QList<ServiceRequest> items;
        for (const auto &v : o.value("items").toArray())
            items << ServiceRequest::fromJson(v.toObject());
        emit requestsDone(items);
    });
}

void KisaClient::getRequest(const QString &id) {
    get("/api/v1/requests/" + id, [this](const QJsonObject &o) {
        emit requestDone(ServiceRequest::fromJson(o));
    });
}

void KisaClient::postTransition(const QString &id, const QString &action,
                                const QString &comment,
                                const QJsonObject &payload) {
    QJsonObject body = payload;
    body["action"] = action;
    if (!comment.isEmpty())
        body["comment"] = comment;
    post("/api/v1/requests/" + id + "/transitions", body,
         [this](const QJsonObject &o) {
             emit transitionDone(ServiceRequest::fromJson(o));
         });
}

void KisaClient::postComment(const QString &id, const QString &text) {
    post("/api/v1/requests/" + id + "/comments", {{"text", text}},
         [](const QJsonObject &) {});
}

void KisaClient::logout() {
    post("/api/v1/auth/logout", {}, [](const QJsonObject &) {});
    m_session->clear();
}

} // namespace kisa
