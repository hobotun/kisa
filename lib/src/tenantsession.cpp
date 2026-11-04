#include "tenantsession.hpp"
#include <QSettings>

namespace kisa {

TenantSession::TenantSession(QObject *parent) : QObject(parent) {
    load();
}

void TenantSession::setSlug(const QString &slug) {
    m_slug = slug;
    save();
}

void TenantSession::setTokens(const QString &access, const QString &refresh) {
    m_access  = access;
    m_refresh = refresh;
    save();
}

void TenantSession::clear() {
    m_access.clear();
    m_refresh.clear();
    save();
    emit cleared();
}

void TenantSession::load() {
    QSettings s;
    s.beginGroup("session/" + m_slug);
    m_slug    = s.value("slug").toString();
    m_access  = s.value("accessToken").toString();
    m_refresh = s.value("refreshToken").toString();
    s.endGroup();
}

void TenantSession::save() const {
    QSettings s;
    s.beginGroup("session/" + m_slug);
    s.setValue("slug",         m_slug);
    s.setValue("accessToken",  m_access);
    s.setValue("refreshToken", m_refresh);
    s.endGroup();
}

} // namespace kisa
