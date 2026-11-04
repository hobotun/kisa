#pragma once
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QStackedWidget>
#include "kisaclient.hpp"
#include "tenantsession.hpp"

namespace kisa {

class RequestsWidget;
class RequestDetailWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(KisaClient *client, TenantSession *session,
                        QWidget *parent = nullptr);

    void onLoggedIn(const UserProfile &user);

private slots:
    void onNavChanged(int row);
    void onRequestSelected(const QString &id);
    void onLogout();
    void onNetworkError(const QString &message);

private:
    KisaClient *m_client;
    TenantSession *m_session;

    QListWidget *m_nav;
    QStackedWidget *m_stack;
    QLabel *m_statusUser;

    RequestsWidget *m_requests;
    RequestDetailWidget *m_detail;

    enum NavItem { NavRequests = 0 };
};

} // namespace kisa
