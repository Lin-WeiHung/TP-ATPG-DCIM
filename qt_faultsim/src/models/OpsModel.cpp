#include "OpsModel.hpp"

OpsModel::OpsModel(MarchModel* model, QObject* parent)
    : QAbstractTableModel(parent), model_(model) {
    connect(model_, &MarchModel::modelReset, this, &OpsModel::onReset);
    connect(model_, &MarchModel::opsChanged, this, &OpsModel::onOpsChanged);
    connect(model_, &MarchModel::selectionChanged, this, &OpsModel::onSelectionChanged);
}

void OpsModel::setElement(int elemIndex){
    elem_ = elemIndex;
    onReset();
}

int OpsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    if (!model_) return 0;
    return model_->opCount(elem_);
}

QVariant OpsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !model_) return {};
    if (role!=Qt::DisplayRole) return {};
    int r = index.row(); int c = index.column();
    auto op = model_->opAt(elem_, r);
    if (!op) return {};
    auto bit=[](Val v){ return v==Val::Zero?"0": v==Val::One?"1":"-"; };
    switch(c){
        case 0: return r;
        case 1: return op->kind==OpKind::Write?"W": op->kind==OpKind::Read?"R":"C";
        case 2: if (op->kind!=OpKind::ComputeAnd) return (op->value==Val::Zero?"0": op->value==Val::One?"1":"-"); return QString(bit(op->C_T));
        case 3: if (op->kind!=OpKind::ComputeAnd) return ""; return QString(bit(op->C_M));
        case 4: if (op->kind!=OpKind::ComputeAnd) return ""; return QString(bit(op->C_B));
    }
    return {};
}

QVariant OpsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role!=Qt::DisplayRole || orientation!=Qt::Horizontal) return {};
    switch(section){
        case 0: return "Idx"; case 1: return "Kind"; case 2: return "Val/T"; case 3: return "M"; case 4: return "B";
    }
    return {};
}

void OpsModel::onReset(){ beginResetModel(); endResetModel(); }
void OpsModel::onOpsChanged(int elem){ if (elem==elem_) onReset(); }
void OpsModel::onSelectionChanged(){ elem_ = model_->selectedElement(); onReset(); }
