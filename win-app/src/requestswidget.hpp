#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QToolBar>
#include "kisaclient.hpp"

namespace kisa {

class RequestsWidget : public QWidget {
    Q_OBJECT
public:
    explicit RequestsWidget(KisaClient *client, QWidget *parent = nullptr);

    void refresh();

signals:
    void requestSelected(const QString &id);

private slots:
    void onRequestsDone(const QList<ServiceRequest> &items);
    void onRowDoubleClicked(int row, int col);

private:
    KisaClient *m_client;
    QTableWidget *m_table;

    void setupTable();
};

} // namespace kisa
