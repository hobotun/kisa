#include "loginwindow.hpp"
#include <QFormLayout>
#include <QVBoxLayout>

namespace kisa {

LoginWindow::LoginWindow(KisaClient *client, TenantSession *session, QWidget *parent)
    : QDialog(parent), m_client(client), m_session(session) {
    setWindowTitle("КИСА — Вход");
    setFixedWidth(340);

    m_tenant   = new QLineEdit(session->slug(), this);
    m_login    = new QLineEdit(this);
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_error = new QLabel(this);
    m_error->setStyleSheet("color: red");
    m_error->hide();
    m_btn = new QPushButton("Войти", this);

    auto *form = new QFormLayout;
    form->addRow("Тенант",  m_tenant);
    form->addRow("Логин",   m_login);
    form->addRow("Пароль",  m_password);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(m_error);
    lay->addWidget(m_btn);

    connect(m_btn, &QPushButton::clicked, this, &LoginWindow::onLogin);
    connect(m_password, &QLineEdit::returnPressed, this, &LoginWindow::onLogin);
    connect(client, &KisaClient::loginDone,   this, &LoginWindow::onLoginDone);
    connect(client, &KisaClient::loginFailed, this, &LoginWindow::onLoginFailed);
}

void LoginWindow::onLogin() {
    m_error->hide();
    m_btn->setEnabled(false);
    m_session->setSlug(m_tenant->text().trimmed());
    m_client->login(m_login->text().trimmed(), m_password->text());
}

void LoginWindow::onLoginDone(const UserProfile &) {
    m_btn->setEnabled(true);
    accept();
}

void LoginWindow::onLoginFailed(const QString &, const QString &message) {
    m_btn->setEnabled(true);
    m_error->setText(message);
    m_error->show();
}

} // namespace kisa
