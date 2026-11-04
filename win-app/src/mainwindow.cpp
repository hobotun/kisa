#include "mainwindow.hpp"
#include "requestdetailwidget.hpp"
#include "requestswidget.hpp"
#include <QDockWidget>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>

namespace kisa {

MainWindow::MainWindow(KisaClient *client, TenantSession *session, QWidget *parent)
    : QMainWindow(parent), m_client(client), m_session(session) {
    setWindowTitle("КИСА");
    resize(1100, 680);

    // --- меню ---
    auto *fileMenu   = menuBar()->addMenu("Файл");
    auto *actLogout  = fileMenu->addAction("Выйти из системы");
    auto *actExit    = fileMenu->addAction("Закрыть");
    connect(actLogout, &QAction::triggered, this, &MainWindow::onLogout);
    connect(actExit,   &QAction::triggered, this, &QMainWindow::close);

    // --- навигация (левая панель) ---
    m_nav = new QListWidget(this);
    m_nav->addItem("Обращения");
    m_nav->setMaximumWidth(160);
    m_nav->setCurrentRow(0);

    // --- виды ---
    m_requests = new RequestsWidget(client, this);
    m_detail   = new RequestDetailWidget(client, this);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_requests); // NavRequests = 0

    // --- правая часть: стек + детали через сплиттер ---
    auto *rightSplit = new QSplitter(Qt::Vertical, this);
    rightSplit->addWidget(m_stack);
    rightSplit->addWidget(m_detail);
    rightSplit->setStretchFactor(0, 2);
    rightSplit->setStretchFactor(1, 1);

    auto *mainSplit = new QSplitter(Qt::Horizontal, this);
    mainSplit->addWidget(m_nav);
    mainSplit->addWidget(rightSplit);
    mainSplit->setStretchFactor(1, 1);
    setCentralWidget(mainSplit);

    // --- статус-бар ---
    m_statusUser = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusUser);

    connect(m_nav, &QListWidget::currentRowChanged, this, &MainWindow::onNavChanged);
    connect(m_requests, &RequestsWidget::requestSelected,
            this, &MainWindow::onRequestSelected);
    connect(client, &KisaClient::networkError, this, &MainWindow::onNetworkError);
}

void MainWindow::onLoggedIn(const UserProfile &user) {
    m_statusUser->setText(
        QString("%1 @ %2").arg(user.name, m_session->slug()));
    m_requests->refresh();
}

void MainWindow::onNavChanged(int row) {
    m_stack->setCurrentIndex(row);
    m_detail->clear();
    if (row == NavRequests)
        m_requests->refresh();
}

void MainWindow::onRequestSelected(const QString &id) {
    m_detail->load(id);
}

void MainWindow::onLogout() {
    m_client->logout();
    // main.cpp слушает session->cleared() и показывает LoginWindow
}

void MainWindow::onNetworkError(const QString &message) {
    statusBar()->showMessage("Ошибка: " + message, 5000);
}

} // namespace kisa
