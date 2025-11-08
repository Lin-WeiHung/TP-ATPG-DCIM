#include "ElementsModel.hpp"

ElementsModel::ElementsModel(MarchModel* model, QObject* parent)
    : QAbstractListModel(parent), model_(model) {
    connect(model_, &MarchModel::modelReset, this, &ElementsModel::onReset);
    connect(model_, &MarchModel::elementListChanged, this, &ElementsModel::onListChanged);
    connect(model_, &MarchModel::elementChanged, this, &ElementsModel::onElemChanged);
}

int ElementsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return model_ ? model_->elementCount() : 0;
}

QVariant ElementsModel::data(const QModelIndex& index, int role) const {
    if (!model_ || !index.isValid()) return {};
    int r = index.row();
    const auto* e = model_->elementAt(r);
    if (!e) return {};
    if (role==Qt::DisplayRole) {
        QString ord = (e->order==AddrOrder::Up?"Up": e->order==AddrOrder::Down?"Down":"Any");
        return QString("E%1 [%2]  ops:%3").arg(r+1).arg(ord).arg((int)e->ops.size());
    }
    if (role==OrderRole) return (int)e->order;
    if (role==OpsCountRole) return (int)e->ops.size();
    return {};
}

void ElementsModel::onReset(){ beginResetModel(); endResetModel(); }
void ElementsModel::onListChanged(){ beginResetModel(); endResetModel(); }
void ElementsModel::onElemChanged(int idx){ emit dataChanged(index(idx,0), index(idx,0)); }
