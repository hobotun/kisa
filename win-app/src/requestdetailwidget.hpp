#pragma once
#include <QLabel>
#include <QTableWidget>
#include <QWidget>
#include "kisaclient.hpp"

namespace kisa {

class RequestDetailWidget : public QWidget {
    Q_OBJECT
public:
    explicit RequestDetailWidget(KisaClient *client, QWidget *parent = nullptr);

    void load(const QString &id);
    void clear();

private slots:
    void onRequestDone(const ServiceRequest &req);
    void onTransitionDone(const ServiceRequest &req);
    void onActionClicked(const QString &action, bool needsComment);

private:
    KisaClient *m_client;
    QString m_currentId;

    QLabel *m_title;
    QLabel *m_state;
    QWidget *m_actionsBar;
    QTableWidget *m_history;

    void populate(const ServiceRequest &req);
    void rebuildActions(const ServiceRequest &req);
};

} // namespace kisa
