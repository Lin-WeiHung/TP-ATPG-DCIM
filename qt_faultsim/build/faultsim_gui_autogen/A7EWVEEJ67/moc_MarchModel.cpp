/****************************************************************************
** Meta object code from reading C++ file 'MarchModel.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.4)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../src/domain/MarchModel.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MarchModel.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.4. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MarchModel_t {
    const uint offsetsAndSize[18];
    char stringdata0[96];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_MarchModel_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_MarchModel_t qt_meta_stringdata_MarchModel = {
    {
QT_MOC_LITERAL(0, 10), // "MarchModel"
QT_MOC_LITERAL(11, 10), // "modelReset"
QT_MOC_LITERAL(22, 0), // ""
QT_MOC_LITERAL(23, 18), // "elementListChanged"
QT_MOC_LITERAL(42, 14), // "elementChanged"
QT_MOC_LITERAL(57, 5), // "index"
QT_MOC_LITERAL(63, 10), // "opsChanged"
QT_MOC_LITERAL(74, 4), // "elem"
QT_MOC_LITERAL(79, 16) // "selectionChanged"

    },
    "MarchModel\0modelReset\0\0elementListChanged\0"
    "elementChanged\0index\0opsChanged\0elem\0"
    "selectionChanged"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MarchModel[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   44,    2, 0x06,    1 /* Public */,
       3,    0,   45,    2, 0x06,    2 /* Public */,
       4,    1,   46,    2, 0x06,    3 /* Public */,
       6,    1,   49,    2, 0x06,    5 /* Public */,
       8,    0,   52,    2, 0x06,    7 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    5,
    QMetaType::Void, QMetaType::Int,    7,
    QMetaType::Void,

       0        // eod
};

void MarchModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MarchModel *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->modelReset(); break;
        case 1: _t->elementListChanged(); break;
        case 2: _t->elementChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->opsChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->selectionChanged(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (MarchModel::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MarchModel::modelReset)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (MarchModel::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MarchModel::elementListChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (MarchModel::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MarchModel::elementChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (MarchModel::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MarchModel::opsChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (MarchModel::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MarchModel::selectionChanged)) {
                *result = 4;
                return;
            }
        }
    }
}

const QMetaObject MarchModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_MarchModel.offsetsAndSize,
    qt_meta_data_MarchModel,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_MarchModel_t
, QtPrivate::TypeAndForceComplete<MarchModel, std::true_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>



>,
    nullptr
} };


const QMetaObject *MarchModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MarchModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MarchModel.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int MarchModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void MarchModel::modelReset()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void MarchModel::elementListChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void MarchModel::elementChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void MarchModel::opsChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void MarchModel::selectionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
