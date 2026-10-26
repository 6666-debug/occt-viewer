// ============================================================================
//  tst_measure.cpp — 测量核心算法的 Qt Test 单元测试
//
//  验证：两个 100mm 的立方体，中心沿 X 轴相距 200mm（即最近表面间距 100mm），
//  用 BRepExtrema_DistShapeShape 计算出的最短距离应为 100mm。
// ============================================================================
#include <QtTest/QtTest>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <TopoDS_Shape.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <gp_Pnt.hxx>
#include <Precision.hxx>

class TestMeasure : public QObject
{
    Q_OBJECT

private slots:
    void distanceBetweenTwoBoxes();
    void distanceBetweenGluedBoxes();
    void boundingBoxOfBox();
};

void TestMeasure::distanceBetweenTwoBoxes()
{
    // 立方体 A：[0,100] x [0,100] x [0,100]
    const TopoDS_Shape boxA = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 100.0, 100.0, 100.0).Shape();
    // 立方体 B：中心相距 200mm -> X 方向 [200,300]，最近表面间距 100mm
    const TopoDS_Shape boxB = BRepPrimAPI_MakeBox(gp_Pnt(200, 0, 0), 100.0, 100.0, 100.0).Shape();

    BRepExtrema_DistShapeShape dist(boxA, boxB);
    dist.Perform();

    QVERIFY(dist.IsDone());
    const double d = dist.Value();
    QVERIFY2(qAbs(d - 100.0) < 1e-6,
             qPrintable(QStringLiteral("期望 100mm，实际 %1mm").arg(d)));
}

void TestMeasure::distanceBetweenGluedBoxes()
{
    // 两个面完全贴合的立方体，距离应为 0
    const TopoDS_Shape boxA = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 100.0, 100.0, 100.0).Shape();
    const TopoDS_Shape boxB = BRepPrimAPI_MakeBox(gp_Pnt(100, 0, 0), 100.0, 100.0, 100.0).Shape();

    BRepExtrema_DistShapeShape dist(boxA, boxB);
    dist.Perform();

    QVERIFY(dist.IsDone());
    QVERIFY2(qAbs(dist.Value()) < Precision::Confusion(),
             qPrintable(QStringLiteral("期望 0mm，实际 %1mm").arg(dist.Value())));
}

void TestMeasure::boundingBoxOfBox()
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(10, 20, 30), 100.0, 100.0, 100.0).Shape();

    Bnd_Box bnd;
    BRepBndLib::Add(box, bnd);
    QVERIFY(!bnd.IsVoid());

    double x1, y1, z1, x2, y2, z2;
    bnd.Get(x1, y1, z1, x2, y2, z2);
    QVERIFY(qAbs(x1 - 10.0) < 1e-6 && qAbs(x2 - 110.0) < 1e-6);
    QVERIFY(qAbs(y1 - 20.0) < 1e-6 && qAbs(y2 - 120.0) < 1e-6);
    QVERIFY(qAbs(z1 - 30.0) < 1e-6 && qAbs(z2 - 130.0) < 1e-6);
}

QTEST_MAIN(TestMeasure)
#include "tst_measure.moc"