#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include "kisaclient.hpp"
#include "tenantsession.hpp"

namespace kisa {

class LoginWindow : public QDialog {
    Q_OBJECT
public:
    explicit LoginWindow(KisaClient *client, TenantSession *session,
                         QWidget *parent = nullptr);

private slots:
    void onLogin();
    void onLoginDone(const UserProfile &user);
    void onLoginFailed(const QString &code, const QString &message);

private:
    KisaClient *m_client;
    TenantSession *m_session;
    QLineEdit *m_tenant;
    QLineEdit *m_login;
    QLineEdit *m_password;
    QLabel *m_error;
    QPushButton *m_btn;
};

} // namespace kisa
