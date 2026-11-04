#include "requestdetailwidget.hpp"
#include <QHBoxLayout>
#include <QInputDialog>
#include <QPushButton>
#include <QVBoxLayout>

namespace kisa {

static const QStringList kHistHeaders = {"Время", "Действие", "Из", "В", "Кто", "Комментарий"};

RequestDetailWidget::RequestDetailWidget(KisaClient *client, QWidget *parent)
    : QWidget(parent), m_client(client) {
    m_title = new QLabel(this);
    m_title->setStyleSheet("font-size: 14pt; font-weight: bold");
    m_state = new QLabel(this);
    m_actionsBar = new QWidget(this);
    m_actionsBar->setLayout(new QHBoxLayout);
    m_actionsBar->layout()->setContentsMargins(0, 0, 0, 0);

    m_history = new QTableWidget(0, kHistHeaders.size(), this);
    m_history->setHorizontalHeaderLabels(kHistHeaders);
    m_history->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_history->horizontalHeader()->setStretchLastSection(true);
    m_history->verticalHeader()->setVisible(false);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_title);
    lay->addWidget(m_state);
    lay->addWidget(m_actionsBar);
    lay->addWidget(m_history);

    connect(m_client, &KisaClient::requestDone,
            this, &RequestDetailWidget::onRequestDone);
    connect(m_client, &KisaClient::transitionDone,
            this, &RequestDetailWidget::onTransitionDone);
}

void RequestDetailWidget::load(const QString &id) {
    m_currentId = id;
    m_client->getRequest(id);
}

void RequestDetailWidget::clear() {
    m_currentId.clear();
    m_title->clear();
    m_state->clear();
    m_history->setRowCount(0);
    qDeleteAll(m_actionsBar->findChildren<QPushButton *>());
}

void RequestDetailWidget::onRequestDone(const ServiceRequest &req) {
    if (!m_currentId.isEmpty() && req.id != m_currentId)
        return;
    populate(req);
}

void RequestDetailWidget::onTransitionDone(const ServiceRequest &req) {
    if (req.id == m_currentId)
        populate(req);
}

void RequestDetailWidget::populate(const ServiceRequest &req) {
    m_title->setText(req.title().isEmpty() ? req.id : req.title());
    m_state->setText(req.stateTitle);
    rebuildActions(req);

    m_history->setRowCount(req.history.size());
    for (int i = 0; i < req.history.size(); ++i) {
        const auto &e = req.history[i];
        m_history->setItem(i, 0, new QTableWidgetItem(
            e.ts.isValid() ? e.ts.toLocalTime().toString("dd.MM.yyyy HH:mm") : ""));
        m_history->setItem(i, 1, new QTableWidgetItem(e.action));
        m_history->setItem(i, 2, new QTableWidgetItem(e.from));
        m_history->setItem(i, 3, new QTableWidgetItem(e.to));
        m_history->setItem(i, 4, new QTableWidgetItem(e.actorLabel));
        m_history->setItem(i, 5, new QTableWidgetItem(e.comment));
    }
    m_history->resizeColumnsToContents();
}

void RequestDetailWidget::rebuildActions(const ServiceRequest &req) {
    qDeleteAll(m_actionsBar->findChildren<QPushButton *>());
    for (const auto &a : req.allowedActions) {
        auto *btn = new QPushButton(a.title, m_actionsBar);
        m_actionsBar->layout()->addWidget(btn);
        const QString action = a.action;
        const bool needsComment = a.needsComment;
        connect(btn, &QPushButton::clicked, this, [this, action, needsComment]() {
            onActionClicked(action, needsComment);
        });
    }
}

void RequestDetailWidget::onActionClicked(const QString &action, bool needsComment) {
    QString comment;
    if (needsComment) {
        bool ok = false;
        comment = QInputDialog::getMultiLineText(this, "Комментарий",
                                                 "Введите комментарий:", {}, &ok);
        if (!ok)
            return;
    }
    m_client->postTransition(m_currentId, action, comment);
}

} // namespace kisa
