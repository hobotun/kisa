#pragma once
#include <QObject>
#include <QString>

namespace kisa {

class TenantSession : public QObject {
    Q_OBJECT
public:
    explicit TenantSession(QObject *parent = nullptr);

    QString slug() const { return m_slug; }
    void setSlug(const QString &slug);

    QString accessToken() const { return m_access; }
    QString refreshToken() const { return m_refresh; }
    void setTokens(const QString &access, const QString &refresh);
    void clear();

    bool isAuthenticated() const { return !m_access.isEmpty(); }

signals:
    void cleared();

private:
    QString m_slug;
    QString m_access;
    QString m_refresh;

    void load();
    void save() const;
};

} // namespace kisa
