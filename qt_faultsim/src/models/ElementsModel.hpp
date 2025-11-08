#pragma once
#include <QAbstractListModel>
#include "../domain/MarchModel.hpp"

class ElementsModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit ElementsModel(MarchModel* model, QObject* parent=nullptr);

    int rowCount(const QModelIndex& parent=QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    enum Roles { OrderRole = Qt::UserRole+1, OpsCountRole };

private slots:
    void onReset();
    void onListChanged();
    void onElemChanged(int idx);

private:
    MarchModel* model_{};
};
