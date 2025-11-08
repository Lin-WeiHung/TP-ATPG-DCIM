#pragma once
#include <QAbstractTableModel>
#include "../domain/MarchModel.hpp"

class OpsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit OpsModel(MarchModel* model, QObject* parent=nullptr);
    void setElement(int elemIndex);

    int rowCount(const QModelIndex& parent=QModelIndex()) const override;
    int columnCount(const QModelIndex& parent=QModelIndex()) const override { (void)parent; return 5; }
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private slots:
    void onReset();
    void onOpsChanged(int elem);
    void onSelectionChanged();

private:
    MarchModel* model_{};
    int elem_{-1};
};
