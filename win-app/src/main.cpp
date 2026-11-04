#include <QApplication>
#include <QSettings>
#include "kisaclient.hpp"
#include "loginwindow.hpp"
#include "mainwindow.hpp"
#include "tenantsession.hpp"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("kisa");
    app.setApplicationName("kisa-win");

    const QString baseUrl =
        qEnvironmentVariable("KISA_URL", "https://kisa.muravyev.online");

    kisa::TenantSession session;
    kisa::KisaClient client(baseUrl, &session);
    kisa::MainWindow mainWin(&client, &session);

    auto showLogin = [&]() {
        kisa::LoginWindow dlg(&client, &session, &mainWin);
        if (dlg.exec() != QDialog::Accepted)
            return false;
        return true;
    };

    // При logout сессия очищается → показываем логин поверх главного окна
    QObject::connect(&session, &kisa::TenantSession::cleared, [&]() {
        mainWin.hide();
        if (showLogin()) {
            // me() вернёт профиль → onLoggedIn
            QObject::connect(&client, &kisa::KisaClient::meDone,
                             &mainWin, &kisa::MainWindow::onLoggedIn,
                             Qt::SingleShotConnection);
            client.me();
            mainWin.show();
        } else {
            app.quit();
        }
    });

    // Первый запуск
    if (session.isAuthenticated()) {
        // Проверяем сессию через /me; при 401 KisaClient сам обновит токен
        QObject::connect(&client, &kisa::KisaClient::meDone,
                         &mainWin, &kisa::MainWindow::onLoggedIn,
                         Qt::SingleShotConnection);
        QObject::connect(&client, &kisa::KisaClient::networkError,
                         [&](const QString &) {
                             // токен протух и refresh не помог — показываем логин
                             if (!session.isAuthenticated()) {
                                 mainWin.hide();
                                 if (!showLogin())
                                     app.quit();
                             }
                         });
        client.me();
        mainWin.show();
    } else {
        if (!showLogin()) {
            return 0;
        }
        QObject::connect(&client, &kisa::KisaClient::meDone,
                         &mainWin, &kisa::MainWindow::onLoggedIn,
                         Qt::SingleShotConnection);
        client.me();
        mainWin.show();
    }

    return app.exec();
}
