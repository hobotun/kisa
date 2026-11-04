#include "requestswidget.hpp"
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>

namespace kisa {

static const QStringList kHeaders = {"Тема", "Стадия", "Приоритет", "Срок"};
static const int kColTitle    = 0;
static const int kColState    = 1;
static const int kColPriority = 2;
static const int kColDue      = 3;

RequestsWidget::RequestsWidget(KisaClient *client, QWidget *parent)
    : QWidget(parent), m_client(client) {
    auto *toolbar = new QToolBar(this);
    auto *btnNew     = toolbar->addAction("Новая заявка");
    auto *btnRefresh = toolbar->addAction("Обновить");
    Q_UNUSED(btnNew) // TODO: открывать диалог создания

    m_table = new QTableWidget(0, kHeaders.size(), this);
    m_table->setHorizontalHeaderLabels(kHeaders);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(toolbar);
    lay->addWidget(m_table);

    connect(btnRefresh, &QAction::triggered, this, &RequestsWidget::refresh);
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &RequestsWidget::onRowDoubleClicked);
    connect(m_client, &KisaClient::requestsDone,
            this, &RequestsWidget::onRequestsDone);
}

void RequestsWidget::refresh() {
    m_client->getRequests();
}

void RequestsWidget::onRequestsDone(const QList<ServiceRequest> &items) {
    m_table->setRowCount(items.size());
    for (int i = 0; i < items.size(); ++i) {
        const auto &r = items[i];
        m_table->setItem(i, kColTitle,    new QTableWidgetItem(r.title()));
        m_table->setItem(i, kColState,    new QTableWidgetItem(r.stateTitle));
        m_table->setItem(i, kColPriority, new QTableWidgetItem(r.priority()));
        m_table->setItem(i, kColDue,
            new QTableWidgetItem(r.dueAt.isValid()
                ? r.dueAt.toLocalTime().toString("dd.MM.yyyy HH:mm") : ""));
        // сохраняем id в первой ячейке для навигации
        m_table->item(i, kColTitle)->setData(Qt::UserRole, r.id);
        if (r.overdue)
            for (int c = 0; c < kHeaders.size(); ++c)
                if (m_table->item(i, c))
                    m_table->item(i, c)->setForeground(Qt::red);
    }
    m_table->resizeColumnsToContents();
}

void RequestsWidget::onRowDoubleClicked(int row, int /*col*/) {
    const auto *item = m_table->item(row, kColTitle);
    if (item)
        emit requestSelected(item->data(Qt::UserRole).toString());
}

} // namespace kisa
