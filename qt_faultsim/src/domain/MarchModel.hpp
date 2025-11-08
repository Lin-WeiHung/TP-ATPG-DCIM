#pragma once
#include <QObject>
#include <QString>
#include <vector>
#include <optional>
#include "../../../include/FaultSimulator.hpp"

// MarchModel wraps a MarchTest and provides edit operations with signals for Qt models.
class MarchModel : public QObject {
    Q_OBJECT
public:
    explicit MarchModel(QObject* parent=nullptr);

    // Accessors
    const MarchTest& test() const { return test_; }
    MarchTest& test() { return test_; }

    // Selection
    int selectedElement() const { return selectedElem_; }
    void setSelectedElement(int idx);
    int cursorOp() const { return cursorOp_; }
    void setCursorOp(int pos);

    // Load/Save
    bool loadFromMarchJson(const QString& path, int index=0, QString* err=nullptr);
    // Minimal serializer: writes a single-entry RawMarchTest array with current test
    bool saveToMarchJson(const QString& path, const QString& name, QString* err=nullptr) const;

    // Element ops
    int elementCount() const { return (int)test_.elements.size(); }
    const MarchElement* elementAt(int i) const;

    void addElement(AddrOrder ord);
    void removeElement(int index);
    void moveElement(int from,int to);
    void setElementOrder(int index, AddrOrder ord);

    // Ops within selected element (or specified element)
    int opCount(int elem) const;
    const Op* opAt(int elem, int pos) const;
    void insertOp(int elem, int pos, const Op& op);
    void removeOp(int elem, int pos);
    void moveOp(int elem, int from, int to);
    void setOp(int elem, int pos, const Op& op);

signals:
    void modelReset();
    void elementListChanged();
    void elementChanged(int index);
    void opsChanged(int elem);
    void selectionChanged();

private:
    MarchTest test_;
    int selectedElem_{-1};
    int cursorOp_{0};
};
