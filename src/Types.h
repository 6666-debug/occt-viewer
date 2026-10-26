#ifndef TYPES_H
#define TYPES_H

#include <QString>
#include <QVector>

// ============================================================
// 共享数据结构（视图层与窗口层共用）
// ============================================================
struct MeasureRecord {
    QString type;
    QString value;
    QString detail;
    QString time;
};

struct PartInfo {
    QString path;
    QString name;
    bool    isAssembly;
    int     parentIdx;
};

enum class MeasureMode {
    None,
    Distance,       // 面到面距离
    Angle,          // 面到面角度
    Radius,         // 半径
    PointToPoint,   // 点到点距离
    Volume          // 体积 + 表面积
};

#endif // TYPES_H
