#ifndef OCCTVIEWPORT_H
#define OCCTVIEWPORT_H

#include "Types.h"

#include <QWidget>
#include <QImage>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <QVector3D>
#include <functional>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_TextLabel.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <TDF_Label.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>

// ============================================================
// 3D 视口：负责 OCCT 视图、STEP 加载、零件树数据、测量交互
// ============================================================
class OcctViewport : public QWidget
{
public:
    explicit OcctViewport(QWidget *parent = nullptr);
    ~OcctViewport() override = default;

    void setMessageCallback(std::function<void(const QString&)> cb) { m_messageCallback = cb; }
    void setRecordCallback(std::function<void(const MeasureRecord&)> cb) { m_recordCallback = cb; }
    void setPartsCallback(std::function<void(const QVector<PartInfo>&)> cb) { m_partsCallback = cb; }

    // XCAF STEP 加载
    bool loadStepXCAF(const QString &fileName);

    // 零件可见性 / 高亮
    void setPartVisible(const QString &path, bool visible);
    void highlightPart(const QString &path);

    // 基础显示
    void showBox();
    void showCylinder();
    void fitAll();
    void setDisplayMode(int mode);
    void setStandardView(int viewType);

    // 剖切
    void setClipEnabled(bool on);
    void setClipAxis(int axis);
    void setClipPosition(int p);

    // 测量
    void setMeasureMode(MeasureMode mode);
    void clearMeasurement();
    void clearLabelsOnly();

    // 截图：把当前 OCCT 帧缓冲导出为图像
    // （OCCT 直接绘制到原生窗口，QWidget::grab 抓不到，需单独导出）
    QImage dumpImage();

    // 测量自检（--selftest-measure）：在视口内扫描并模拟拾取，验证 5 种测量模式
    QStringList measureSelfTest();

    // 演示模式（--demo-measure）：自动挑两个相距最远的面做一次距离测量，
    // 生成带 3D 标注（红色高亮面 + 尺寸标签 + 历史表格行）的截图
    bool autoMeasureDistance();

protected:
    QPaintEngine *paintEngine() const override;
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *e) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    static int safePixelSize(int logical, qreal dpr);

    // XCAF 树递归
    void addLabelToTree(const TDF_Label &label, int parentIdx,
                        QVector<PartInfo> &parts,
                        const Handle(XCAFDoc_ShapeTool) &shapeTool,
                        const Handle(XCAFDoc_ColorTool) &colorTool);

    Quantity_Color resolveShapeColor(const TopoDS_Shape &shape,
                                     const TDF_Label &refLabel,
                                     const TDF_Label &instLabel,
                                     const Handle(XCAFDoc_ShapeTool) &shapeTool,
                                     const Handle(XCAFDoc_ColorTool) &colorTool,
                                     bool *outFound = nullptr);

    QString getLabelName(const TDF_Label &label);
    void disableHoverHighlight();
    void resetMeasureState();
    void clearRedShapes();
    void clearTextLabels();
    void showRedShape(const TopoDS_Shape &shape);
    void showTextLabel(const QString &text, const gp_Pnt &pos);
    gp_Pnt getFaceCenter(const TopoDS_Face &face);
    void addRecord(const QString &type, const QString &value, const QString &detail);
    void applyDisplayMode();

    bool getFaceNormal(const TopoDS_Face &face, gp_Dir &normal);
    bool getFaceRadius(const TopoDS_Face &face, double &radius, QString &typeName);
    void doMeasurePick(const QPoint &pos);
    void handleDistance(const TopoDS_Shape &picked);
    void handleAngle(const TopoDS_Face &face);
    void handleRadius(const TopoDS_Face &face);
    void handlePointToPoint(const TopoDS_Shape &picked);
    void handleVolume(const TopoDS_Shape &picked);

    void initClipPlane();
    void updateBoundingBox(const TopoDS_Shape &shape);
    void refreshClipPlane();

private:
    Handle(Aspect_DisplayConnection) m_displayConnection;
    Handle(OpenGl_GraphicDriver)     m_graphicDriver;
    Handle(V3d_Viewer)               m_viewer;
    Handle(AIS_InteractiveContext)   m_context;
    Handle(V3d_View)                 m_view;
    Handle(Aspect_NeutralWindow)     m_window;
    Handle(TDocStd_Document)         m_doc;
    QPoint                           m_lastPos;

    Handle(AIS_Shape) m_currentShape;
    QVector<Handle(AIS_Shape)> m_redShapes;
    QVector<Handle(AIS_TextLabel)> m_textLabels;
    QHash<QString, Handle(AIS_Shape)> m_pathToShape;

    int m_displayMode = 0;

    Handle(Graphic3d_ClipPlane) m_clipPlane;
    int m_clipAxis = 0;
    int m_clipPercent = 0;

    MeasureMode  m_measureMode = MeasureMode::None;
    TopoDS_Shape m_measureFirst;
    gp_Dir       m_measureFirstNormal;
    gp_Pnt       m_measureFirstCenter;

    QVector3D m_bboxMin = QVector3D(-100, -100, -100);
    QVector3D m_bboxMax = QVector3D( 100,  100,  100);

    std::function<void(const QString&)> m_messageCallback;
    std::function<void(const MeasureRecord&)> m_recordCallback;
    std::function<void(const QVector<PartInfo>&)> m_partsCallback;
};

#endif // OCCTVIEWPORT_H
