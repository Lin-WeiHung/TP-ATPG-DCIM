#include "MarchModel.hpp"
#include <QFile>
#include <QTextStream>

MarchModel::MarchModel(QObject* parent) : QObject(parent) {
    // Start with empty test
    test_.name = "Untitled";
}

void MarchModel::setSelectedElement(int idx){
    if (idx<0 || idx>= (int)test_.elements.size()) idx = -1;
    if (selectedElem_==idx) return;
    selectedElem_ = idx;
    // reset cursor
    cursorOp_ = 0;
    if (selectedElem_>=0) cursorOp_ = (int)test_.elements[selectedElem_].ops.size();
    emit selectionChanged();
}

void MarchModel::setCursorOp(int pos){
    if (selectedElem_<0) return;
    int n = (int)test_.elements[selectedElem_].ops.size();
    if (pos<0) pos=0; if (pos>n) pos=n;
    if (cursorOp_==pos) return;
    cursorOp_ = pos;
    emit selectionChanged();
}

const MarchElement* MarchModel::elementAt(int i) const {
    if (i<0 || i>=(int)test_.elements.size()) return nullptr;
    return &test_.elements[i];
}

void MarchModel::addElement(AddrOrder ord){
    MarchElement e; e.order = ord;
    test_.elements.push_back(e);
    emit elementListChanged();
}

void MarchModel::removeElement(int index){
    if (index<0 || index>=(int)test_.elements.size()) return;
    test_.elements.erase(test_.elements.begin()+index);
    if (selectedElem_==index) selectedElem_=-1;
    else if (selectedElem_>index) selectedElem_--;
    emit elementListChanged();
}

void MarchModel::moveElement(int from,int to){
    if (from<0||from>=(int)test_.elements.size()||to<0||to>=(int)test_.elements.size()) return;
    if (from==to) return;
    auto e = test_.elements[from];
    test_.elements.erase(test_.elements.begin()+from);
    test_.elements.insert(test_.elements.begin()+to, e);
    emit elementListChanged();
}

void MarchModel::setElementOrder(int index, AddrOrder ord){
    if (index<0 || index>=(int)test_.elements.size()) return;
    test_.elements[index].order = ord;
    emit elementChanged(index);
}

int MarchModel::opCount(int elem) const {
    if (elem<0 || elem>=(int)test_.elements.size()) return 0;
    return (int)test_.elements[elem].ops.size();
}

const Op* MarchModel::opAt(int elem, int pos) const {
    if (elem<0 || elem>=(int)test_.elements.size()) return nullptr;
    const auto& v = test_.elements[elem].ops;
    if (pos<0 || pos>=(int)v.size()) return nullptr;
    return &v[pos];
}

void MarchModel::insertOp(int elem, int pos, const Op& op){
    if (elem<0 || elem>=(int)test_.elements.size()) return;
    auto& v = test_.elements[elem].ops;
    if (pos<0) pos=0; if (pos>(int)v.size()) pos=(int)v.size();
    v.insert(v.begin()+pos, op);
    emit opsChanged(elem);
}

void MarchModel::removeOp(int elem, int pos){
    if (elem<0 || elem>=(int)test_.elements.size()) return;
    auto& v = test_.elements[elem].ops;
    if (pos<0 || pos>=(int)v.size()) return;
    v.erase(v.begin()+pos);
    emit opsChanged(elem);
}

void MarchModel::moveOp(int elem, int from, int to){
    if (elem<0 || elem>=(int)test_.elements.size()) return;
    auto& v = test_.elements[elem].ops;
    if (from<0 || from>=(int)v.size() || to<0 || to>=(int)v.size()) return;
    auto x = v[from];
    v.erase(v.begin()+from);
    v.insert(v.begin()+to, x);
    emit opsChanged(elem);
}

void MarchModel::setOp(int elem, int pos, const Op& op){
    if (elem<0 || elem>=(int)test_.elements.size()) return;
    auto& v = test_.elements[elem].ops;
    if (pos<0 || pos>=(int)v.size()) return;
    v[pos] = op;
    emit opsChanged(elem);
}

bool MarchModel::loadFromMarchJson(const QString& path, int index, QString* err){
    try{
        MarchTestJsonParser p;
        auto raws = p.parse_file(path.toStdString());
        if (raws.empty()) { if (err) *err = "no March tests in JSON"; return false; }
        if (index<0 || index>=(int)raws.size()) index = 0;
        MarchTestNormalizer norm;
        test_ = norm.normalize(raws[index]);
        selectedElem_ = test_.elements.empty() ? -1 : 0;
        cursorOp_ = (selectedElem_>=0) ? (int)test_.elements[0].ops.size() : 0;
        emit modelReset();
        emit elementListChanged();
        if (selectedElem_>=0) emit opsChanged(selectedElem_);
        emit selectionChanged();
        return true;
    }catch(const std::exception& e){ if (err) *err = e.what(); return false; }
}

static QString orderChar(AddrOrder o){
    switch(o){ case AddrOrder::Up: return "a"; case AddrOrder::Down: return "d"; case AddrOrder::Any: default: return "b"; }
}

static QString opToToken(const Op& op){
    if (op.kind==OpKind::Write){ return QString("W%1").arg(op.value==Val::Zero?"0":"1"); }
    if (op.kind==OpKind::Read){ return QString("R%1").arg(op.value==Val::Zero?"0":"1"); }
    auto bit=[&](Val v){ return v==Val::Zero?"0": v==Val::One?"1":"-"; };
    // Serializer follows C(0)(1)(0) format but replaces X with 0 for compatibility
    auto b2=[&](Val v){ return v==Val::Zero?"0": v==Val::One?"1":"0"; };
    return QString("C(%1)(%2)(%3)").arg(b2(op.C_T), b2(op.C_M), b2(op.C_B));
}

bool MarchModel::saveToMarchJson(const QString& path, const QString& name, QString* err) const {
    try{
        QFile f(path);
        if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate|QIODevice::Text)){
            if(err)*err = f.errorString(); return false;
        }
        QTextStream ts(&f);
        ts << "[\n  {\n    \"March_test\": \"" << name << "\",\n    \"Pattern\": \"";
        bool firstE=true;
        for (const auto& e : test_.elements){
            if(!firstE) ts << ";"; firstE=false;
            ts << orderChar(e.order) << "(";
            bool firstOp=true;
            for (const auto& op : e.ops){ if(!firstOp) ts << ","; firstOp=false; ts << opToToken(op); }
            ts << ")";
        }
        ts << "\"\n  }\n]\n";
        return true;
    }catch(const std::exception& e){ if(err)*err = e.what(); return false; }
}
