#include "OcctViewport.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QKeyEvent>
#include <QPaintEngine>
#include <QResizeEvent>
#include <QShowEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cmath>
#include <algorithm>

#include <AIS_InteractiveObject.hxx>
#include <AIS_KindOfInteractive.hxx>
#include <AIS_DisplayMode.hxx>
#include <AIS_Shape.hxx>
#include <AIS_TextLabel.hxx>
#include <Quantity_Color.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <BRep_Tool.hxx>
#include <Graphic3d_RenderingParams.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Graphic3d_NameOfMaterial.hxx>
#include <Prs3d_Drawer.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <BRepExtrema_DistShapeShape.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <gp_Vec.hxx>
#include <BRepLProp_SLProps.hxx>
#include <Standard_Failure.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <gp_Pln.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <NCollection_Sequence.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <Precision.hxx>

#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ColorType.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_Tool.hxx>
#include <TDataStd_Name.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TCollection_AsciiString.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <TopLoc_Location.hxx>
#include <XCAFDoc_Location.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <TDocStd_Document.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <gp_Pln.hxx>

OcctViewport::OcctViewport(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    winId();

    m_displayConnection = new Aspect_DisplayConnection();
    m_graphicDriver     = new OpenGl_GraphicDriver(m_displayConnection);
    m_viewer            = new V3d_Viewer(m_graphicDriver);
    m_viewer->SetDefaultLights();
    m_viewer->SetLightOn();
    m_context = new AIS_InteractiveContext(m_viewer);

    disableHoverHighlight();

    m_view = m_viewer->CreateView();
    m_window = new Aspect_NeutralWindow();
    m_window->SetNativeHandle(reinterpret_cast<Aspect_Drawable>(winId()));

    qreal dpr = devicePixelRatio();
    m_window->SetSize(safePixelSize(width(), dpr), safePixelSize(height(), dpr));
    m_view->SetWindow(m_window);

    Graphic3d_RenderingParams& params = m_view->ChangeRenderingParams();
    params.Method = Graphic3d_RM_RASTERIZATION;
    params.TransparencyMethod = Graphic3d_RTM_BLEND_UNORDERED;

    initClipPlane();
    showBox();
}

int OcctViewport::safePixelSize(int logical, qreal dpr)
{
    if (!(dpr > 0.0) || !std::isfinite(dpr)) dpr = 1.0;
    double px = logical * dpr;
    if (!std::isfinite(px) || px < 1.0) px = 1.0;
    return static_cast<int>(px);
}

// ============================================================
// XCAF STEP 加载
// ============================================================
bool OcctViewport::loadStepXCAF(const QString &fileName)
{
    // 支持 Qt 资源路径（":/..."）与 qrc: 前缀：OCCT 的 STEP 读取器只能打开真实
    // 文件系统路径，所以先把内嵌资源释放到临时目录，再交给 OCCT 加载。
    QTemporaryDir tempDir;
    QString sourcePath = fileName;

    if (fileName.startsWith(QLatin1Char(':')) || fileName.startsWith(QLatin1String("qrc:"))) {
        if (!tempDir.isValid()) {
            if (m_messageCallback) m_messageCallback(QStringLiteral("无法创建临时目录，内嵌模型加载失败"));
            return false;
        }
        QFile in(fileName);
        if (!in.open(QIODevice::ReadOnly)) {
            if (m_messageCallback) m_messageCallback(QStringLiteral("无法读取内嵌模型：%1").arg(fileName));
            return false;
        }
        const QString extracted = tempDir.filePath(QFileInfo(fileName).fileName());
        QFile out(extracted);
        if (!out.open(QIODevice::WriteOnly)) {
            if (m_messageCallback) m_messageCallback(QStringLiteral("无法写入临时文件：%1").arg(extracted));
            return false;
        }
        out.write(in.readAll());
        out.close();
        in.close();
        sourcePath = extracted;
    }

    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) doc;
    app->NewDocument("MDTV-XCAF", doc);

    STEPCAFControl_Reader reader;
    reader.SetColorMode(true);
    reader.SetNameMode(true);
    reader.SetLayerMode(true);

    IFSelect_ReturnStatus status = reader.ReadFile(sourcePath.toStdString().c_str());
    if (status != IFSelect_RetDone) return false;
    if (!reader.Transfer(doc)) return false;

    m_context->RemoveAll(Standard_False);
    m_context->ClearSelected(Standard_False);
    clearRedShapes();
    clearTextLabels();
    resetMeasureState();
    m_pathToShape.clear();

    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

    QVector<PartInfo> parts;
    TDF_LabelSequence freeShapes;
    shapeTool->GetFreeShapes(freeShapes);

    for (int i = 1; i <= freeShapes.Length(); ++i) {
        addLabelToTree(freeShapes.Value(i), -1, parts, shapeTool, colorTool);
    }

    m_currentShape.Nullify();
    if (m_partsCallback) m_partsCallback(parts);

    // 更新整体包围盒
    double xmin = 1e30, ymin = 1e30, zmin = 1e30;
    double xmax = -1e30, ymax = -1e30, zmax = -1e30;
    bool hasBox = false;

    for (auto it = m_pathToShape.begin(); it != m_pathToShape.end(); ++it) {
        Handle(AIS_Shape) ais = it.value();
        if (ais.IsNull()) continue;
        Bnd_Box box;
        BRepBndLib::Add(ais->Shape(), box);
        if (box.IsVoid()) continue;
        double bx1, by1, bz1, bx2, by2, bz2;
        box.Get(bx1, by1, bz1, bx2, by2, bz2);
        xmin = std::min(xmin, bx1); ymin = std::min(ymin, by1); zmin = std::min(zmin, bz1);
        xmax = std::max(xmax, bx2); ymax = std::max(ymax, by2); zmax = std::max(zmax, bz2);
        hasBox = true;
    }

    if (hasBox) {
        m_bboxMin = QVector3D(xmin, ymin, zmin);
        m_bboxMax = QVector3D(xmax, ymax, zmax);
        refreshClipPlane();
    }

    applyDisplayMode();   // XCAF 装配体同样遵循当前显示模式（默认实体着色）
    m_view->FitAll();
    m_view->Redraw();

    m_context->Activate(AIS_Shape::SelectionMode(TopAbs_FACE));
    m_doc = doc;

    qDebug() << "XCAF 加载成功，零件数：" << parts.size();
    return true;
}

void OcctViewport::setPartVisible(const QString &path, bool visible)
{
    if (!m_pathToShape.contains(path)) return;
    Handle(AIS_Shape) ais = m_pathToShape[path];
    if (ais.IsNull()) return;

    if (!visible && !m_measureFirst.IsNull()) {
        resetMeasureState();
        clearRedShapes();
        clearTextLabels();
    }

    if (visible) m_context->Display(ais, Standard_False);
    else m_context->Erase(ais, Standard_False);
    m_view->Redraw();
}

void OcctViewport::highlightPart(const QString &path)
{
    m_context->ClearSelected(Standard_False);
    if (m_pathToShape.contains(path)) {
        Handle(AIS_Shape) ais = m_pathToShape[path];
        if (!ais.IsNull() && m_context->IsDisplayed(ais)) {
            m_context->SetSelected(ais, Standard_False);
        }
    }
    m_view->Redraw();
}

// ============================================================
// 基础显示
// ============================================================
void OcctViewport::showBox()
{
    m_context->RemoveAll(Standard_False);
    m_context->ClearSelected(Standard_False);
    clearRedShapes();
    clearTextLabels();
    resetMeasureState();
    m_pathToShape.clear();
    m_doc.Nullify();
    if (m_partsCallback) m_partsCallback(QVector<PartInfo>());

    BRepPrimAPI_MakeBox aBox(100.0, 100.0, 100.0);
    m_currentShape = new AIS_Shape(aBox.Shape());
    m_currentShape->SetColor(Quantity_Color(Quantity_NOC_GOLD));
    m_context->Display(m_currentShape, Standard_False);
    m_context->Activate(AIS_Shape::SelectionMode(TopAbs_FACE));

    applyDisplayMode();
    updateBoundingBox(m_currentShape->Shape());
    refreshClipPlane();
    m_view->FitAll();
    m_view->Redraw();
}

void OcctViewport::showCylinder()
{
    m_context->RemoveAll(Standard_False);
    m_context->ClearSelected(Standard_False);
    clearRedShapes();
    clearTextLabels();
    resetMeasureState();
    m_pathToShape.clear();
    m_doc.Nullify();
    if (m_partsCallback) m_partsCallback(QVector<PartInfo>());

    BRepPrimAPI_MakeCylinder aCyl(50.0, 100.0);
    m_currentShape = new AIS_Shape(aCyl.Shape());
    m_currentShape->SetColor(Quantity_Color(Quantity_NOC_GOLD));
    m_context->Display(m_currentShape, Standard_False);
    m_context->Activate(AIS_Shape::SelectionMode(TopAbs_FACE));

    applyDisplayMode();
    updateBoundingBox(m_currentShape->Shape());
    refreshClipPlane();
    m_view->FitAll();
    m_view->Redraw();
}

void OcctViewport::fitAll() { m_view->FitAll(); m_view->Redraw(); }

void OcctViewport::setDisplayMode(int mode) { m_displayMode = mode; applyDisplayMode(); }

void OcctViewport::setStandardView(int viewType)
{
    switch (viewType) {
    case 0: m_view->SetProj(V3d_Xpos); break;
    case 1: m_view->SetProj(V3d_Xneg); break;
    case 2: m_view->SetProj(V3d_Ypos); break;
    case 3: m_view->SetProj(V3d_Yneg); break;
    case 4: m_view->SetProj(V3d_Zpos); break;
    case 5: m_view->SetProj(V3d_Zneg); break;
    case 6: m_view->SetProj(V3d_XposYnegZpos); break;
    }
    m_view->FitAll();
    m_view->Redraw();
}

void OcctViewport::setClipEnabled(bool on) {
    if (!m_clipPlane.IsNull()) m_clipPlane->SetOn(on);
    m_view->Redraw();
}
void OcctViewport::setClipAxis(int axis) { m_clipAxis = axis; refreshClipPlane(); m_view->Redraw(); }
void OcctViewport::setClipPosition(int p) { m_clipPercent = p; refreshClipPlane(); m_view->Redraw(); }

// ============================================================
// 测量模式切换
// ============================================================
void OcctViewport::setMeasureMode(MeasureMode mode)
{
    m_measureMode = mode;
    m_context->ClearSelected(Standard_False);
    clearRedShapes();
    clearTextLabels();
    resetMeasureState();

    // 根据测量模式切换选择模式
    m_context->Deactivate();
    switch (mode) {
    case MeasureMode::PointToPoint:
        m_context->Activate(AIS_Shape::SelectionMode(TopAbs_VERTEX));
        break;
    case MeasureMode::Volume:
        m_context->Activate(AIS_Shape::SelectionMode(TopAbs_SOLID));
        break;
    case MeasureMode::None:
        m_context->Activate(AIS_Shape::SelectionMode(TopAbs_FACE));
        break;
    default:
        m_context->Activate(AIS_Shape::SelectionMode(TopAbs_FACE));
        break;
    }
    m_view->Redraw();

    if (m_messageCallback) {
        switch (mode) {
        case MeasureMode::None:
            m_messageCallback(QStringLiteral("已退出测量模式"));
            break;
        case MeasureMode::Distance:
            m_messageCallback(QStringLiteral("测距离：点击两个面"));
            break;
        case MeasureMode::Angle:
            m_messageCallback(QStringLiteral("测角度：点击两个面"));
            break;
        case MeasureMode::Radius:
            m_messageCallback(QStringLiteral("测半径：点击一个圆柱/球/圆锥面"));
            break;
        case MeasureMode::PointToPoint:
            m_messageCallback(QStringLiteral("测点到点：点击两个顶点"));
            break;
        case MeasureMode::Volume:
            m_messageCallback(QStringLiteral("测体积/表面积：点击一个实体"));
            break;
        }
    }
}

void OcctViewport::clearMeasurement()
{
    resetMeasureState();
    m_context->ClearSelected(Standard_False);
    clearRedShapes();
    clearTextLabels();
    m_view->Redraw();
    if (m_messageCallback)
        m_messageCallback(QStringLiteral("已清除测量"));
}

void OcctViewport::clearLabelsOnly()
{
    clearTextLabels();
    clearRedShapes();
    m_view->Redraw();
}

// ============================================================
// Qt 事件
// ============================================================
QPaintEngine *OcctViewport::paintEngine() const { return nullptr; }

void OcctViewport::showEvent(QShowEvent *)
{
    if (!m_view.IsNull() && !m_window.IsNull()) {
        qreal dpr = devicePixelRatio();
        m_window->SetSize(safePixelSize(width(), dpr), safePixelSize(height(), dpr));
        m_view->MustBeResized();
        m_view->FitAll();
        m_view->Redraw();
    }
}

void OcctViewport::resizeEvent(QResizeEvent *e)
{
    if (!m_view.IsNull() && !m_window.IsNull()) {
        qreal dpr = devicePixelRatio();
        m_window->SetSize(safePixelSize(e->size().width(), dpr),
                          safePixelSize(e->size().height(), dpr));
        m_view->MustBeResized();
    }
}

QImage OcctViewport::dumpImage()
{
    if (m_view.IsNull() || m_window.IsNull())
        return QImage();

    QTemporaryDir dir;
    if (!dir.isValid())
        return QImage();

    const QString path = dir.filePath(QStringLiteral("viewport.png"));
    m_view->Redraw();
    if (!m_view->Dump(path.toStdString().c_str()))
        return QImage();

    return QImage(path);
}

void OcctViewport::paintEvent(QPaintEvent *) { if (!m_view.IsNull()) m_view->Redraw(); }

void OcctViewport::mousePressEvent(QMouseEvent *e)
{
    if (m_measureMode != MeasureMode::None && e->button() == Qt::LeftButton) {
        doMeasurePick(e->pos());
        return;
    }
    m_lastPos = e->pos();
}

void OcctViewport::mouseMoveEvent(QMouseEvent *e)
{
    if (m_measureMode != MeasureMode::None && (e->buttons() & Qt::RightButton)) {
        m_view->Rotation(e->pos().x(), e->pos().y());
        return;
    }
    if (e->buttons() & Qt::LeftButton)
        m_view->Rotation(e->pos().x(), e->pos().y());
    else if (e->buttons() & Qt::RightButton) {
        m_view->Pan(e->pos().x() - m_lastPos.x(), m_lastPos.y() - e->pos().y());
        m_lastPos = e->pos();
    }
}

void OcctViewport::wheelEvent(QWheelEvent *e)
{
    const Standard_Real k = (e->angleDelta().y() > 0) ? 1.1 : 1.0 / 1.1;
    m_view->SetZoom(k);
    m_view->Redraw();
}

void OcctViewport::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) clearMeasurement();
    QWidget::keyPressEvent(e);
}

// ============================================================
// XCAF 树递归
// ============================================================
void OcctViewport::addLabelToTree(const TDF_Label &label, int parentIdx,
                    QVector<PartInfo> &parts,
                    const Handle(XCAFDoc_ShapeTool) &shapeTool,
                    const Handle(XCAFDoc_ColorTool) &colorTool)
{
    if (label.IsNull()) return;

    PartInfo info;
    info.parentIdx = parentIdx;
    info.name = getLabelName(label);

    TCollection_AsciiString entry;
    TDF_Tool::Entry(label, entry);
    info.path = QString::fromUtf8(entry.ToCString());

    if (shapeTool->IsAssembly(label)) {
        info.isAssembly = true;
        int myIdx = parts.size();
        parts.append(info);

        TDF_LabelSequence components;
        shapeTool->GetComponents(label, components);
        for (int i = 1; i <= components.Length(); ++i) {
            addLabelToTree(components.Value(i), myIdx, parts, shapeTool, colorTool);
        }
    } else {
        info.isAssembly = false;

        TDF_Label refLabel = label;
        if (shapeTool->IsReference(label)) {
            shapeTool->GetReferredShape(label, refLabel);
        }

        TopoDS_Shape shape = shapeTool->GetShape(label);
        if (shape.IsNull()) {
            shape = shapeTool->GetShape(refLabel);
            if (shape.IsNull()) return;
            TopLoc_Location loc = shapeTool->GetLocation(label);
            shape.Move(loc);
        }

        // 单位异常检测
        try {
            Bnd_Box tmpBox;
            BRepBndLib::Add(shape, tmpBox);
            if (!tmpBox.IsVoid()) {
                double x1, y1, z1, x2, y2, z2;
                tmpBox.Get(x1, y1, z1, x2, y2, z2);
                double maxDim = std::max({x2-x1, y2-y1, z2-z1});
                if (maxDim > 10000.0) {
                    gp_Trsf scaleTrsf;
                    scaleTrsf.SetScale(gp_Pnt(0, 0, 0), 0.001);
                    BRepBuilderAPI_Transform transformer(shape, scaleTrsf, true);
                    if (transformer.IsDone()) {
                        shape = transformer.Shape();
                        qDebug() << "[UNIT] 单实体尺寸" << maxDim << "，已缩放 0.001";
                    }
                }
            }
        } catch (Standard_Failure &) {}

        NCollection_Sequence<TopoDS_Shape> solids;
        for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) {
            solids.Append(ex.Current());
        }

        if (solids.Length() > 1) {
            info.isAssembly = true;
            int myIdx = parts.size();
            parts.append(info);

            for (int si = 1; si <= solids.Length(); ++si) {
                TopoDS_Shape solidShape = solids.Value(si);

                // 单位异常检测
                try {
                    Bnd_Box tmpBox;
                    BRepBndLib::Add(solidShape, tmpBox);
                    if (!tmpBox.IsVoid()) {
                        double x1, y1, z1, x2, y2, z2;
                        tmpBox.Get(x1, y1, z1, x2, y2, z2);
                        double maxDim = std::max({x2-x1, y2-y1, z2-z1});
                        if (maxDim > 10000.0) {
                            gp_Trsf scaleTrsf;
                            scaleTrsf.SetScale(gp_Pnt(0, 0, 0), 0.001);
                            BRepBuilderAPI_Transform transformer(solidShape, scaleTrsf, true);
                            if (transformer.IsDone()) {
                                solidShape = transformer.Shape();
                                qDebug() << "[UNIT] 实体" << si << "尺寸" << maxDim << "，已缩放 0.001";
                            }
                        }
                    }
                } catch (Standard_Failure &) {}

                PartInfo solidInfo;
                solidInfo.parentIdx = myIdx;
                solidInfo.isAssembly = false;
                solidInfo.name = QStringLiteral("%1 [实体 %2]").arg(info.name).arg(si);
                solidInfo.path = QStringLiteral("%1:solid_%2").arg(info.path).arg(si);

                Handle(AIS_Shape) solidAis = new AIS_Shape(solidShape);
                bool solidHasColor = false;
                Quantity_Color solidColor = resolveShapeColor(
                    solidShape, refLabel, label, shapeTool, colorTool, &solidHasColor);
                solidAis->SetColor(solidColor);

                m_context->Display(solidAis, Standard_False);
                m_context->SetZLayer(solidAis, Graphic3d_ZLayerId_Default);
                m_pathToShape[solidInfo.path] = solidAis;
                parts.append(solidInfo);
            }
            return;
        }

        // 单实体
        Handle(AIS_Shape) ais = new AIS_Shape(shape);
        Quantity_Color color;
        bool hasColor = false;
        color = resolveShapeColor(shape, refLabel, label, shapeTool, colorTool, &hasColor);
        if (hasColor) ais->SetColor(color);
        else ais->SetColor(Quantity_Color(Quantity_NOC_GOLD));

        m_context->Display(ais, Standard_False);
        m_context->SetZLayer(ais, Graphic3d_ZLayerId_Default);
        m_pathToShape[info.path] = ais;
        parts.append(info);
    }
}

Quantity_Color OcctViewport::resolveShapeColor(const TopoDS_Shape &shape,
                                 const TDF_Label &refLabel,
                                 const TDF_Label &instLabel,
                                 const Handle(XCAFDoc_ShapeTool) &shapeTool,
                                 const Handle(XCAFDoc_ColorTool) &colorTool,
                                 bool *outFound)
{
    Quantity_Color color;
    bool found = false;

    if (!found && colorTool->GetColor(refLabel, XCAFDoc_ColorSurf, color)) found = true;
    if (!found && colorTool->GetColor(refLabel, XCAFDoc_ColorGen, color)) found = true;
    if (!found && colorTool->GetColor(refLabel, XCAFDoc_ColorCurv, color)) found = true;

    if (!found && !instLabel.IsEqual(refLabel)) {
        if (colorTool->GetColor(instLabel, XCAFDoc_ColorSurf, color)) found = true;
        else if (colorTool->GetColor(instLabel, XCAFDoc_ColorGen, color)) found = true;
    }

    if (!found) {
        for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More() && !found; ex.Next()) {
            const TopoDS_Shape &solid = ex.Current();
            if (colorTool->GetColor(solid, XCAFDoc_ColorSurf, color) ||
                colorTool->GetColor(solid, XCAFDoc_ColorGen, color)) { found = true; break; }

            TDF_Label solidLabel;
            if (shapeTool->Search(solid, solidLabel) && !solidLabel.IsNull()) {
                if (colorTool->GetColor(solidLabel, XCAFDoc_ColorSurf, color) ||
                    colorTool->GetColor(solidLabel, XCAFDoc_ColorGen, color)) { found = true; break; }
            }
        }
    }

    if (!found) {
        for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Shape &face = ex.Current();
            if (colorTool->GetColor(face, XCAFDoc_ColorSurf, color) ||
                colorTool->GetColor(face, XCAFDoc_ColorGen, color)) { found = true; break; }

            TDF_Label faceLabel;
            if (shapeTool->Search(face, faceLabel) && !faceLabel.IsNull()) {
                if (colorTool->GetColor(faceLabel, XCAFDoc_ColorSurf, color) ||
                    colorTool->GetColor(faceLabel, XCAFDoc_ColorGen, color)) { found = true; break; }
            }
        }
    }

    if (outFound) *outFound = found;
    return found ? color : Quantity_Color(Quantity_NOC_GOLD);
}

QString OcctViewport::getLabelName(const TDF_Label &label)
{
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_ExtendedString name = nameAttr->Get();
        const Standard_ExtString ext = name.ToExtString();
        return QString::fromUtf16(reinterpret_cast<const char16_t*>(ext));
    }
    return QStringLiteral("Shape_%1").arg(label.Tag());
}

void OcctViewport::disableHoverHighlight()
{
    Handle(Prs3d_Drawer) hoverStyle = m_context->HighlightStyle();
    if (!hoverStyle.IsNull()) {
        hoverStyle->SetDisplayMode(AIS_Shaded);
        hoverStyle->SetTransparency(0.999);
    }
}

void OcctViewport::resetMeasureState() { m_measureFirst = TopoDS_Shape(); }

void OcctViewport::clearRedShapes()
{
    for (auto &shape : m_redShapes) {
        if (!shape.IsNull()) m_context->Remove(shape, Standard_False);
    }
    m_redShapes.clear();
}

void OcctViewport::clearTextLabels()
{
    for (auto &label : m_textLabels) {
        if (!label.IsNull()) m_context->Remove(label, Standard_False);
    }
    m_textLabels.clear();
}

// 通用高亮：支持面、顶点、实体
void OcctViewport::showRedShape(const TopoDS_Shape &shape)
{
    if (shape.IsNull()) return;
    clearRedShapes();

    TopAbs_ShapeEnum type = shape.ShapeType();

    try {
        if (type == TopAbs_FACE) {
            Handle(AIS_Shape) redShape = new AIS_Shape(shape);
            redShape->SetColor(Quantity_Color(Quantity_NOC_RED));
            redShape->SetTransparency(0.0);
            redShape->SetDisplayMode(AIS_Shaded);
            m_context->Display(redShape, Standard_False);
            m_context->SetZLayer(redShape, Graphic3d_ZLayerId_Top);
            m_context->Deactivate(redShape);
            m_redShapes.append(redShape);
        }
        else if (type == TopAbs_VERTEX) {
            // 顶点：画小球
            gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(shape));
            double size = (m_bboxMax - m_bboxMin).length();
            if (!std::isfinite(size) || size <= 1e-9) size = 100.0;
            double radius = std::max(0.5, size * 0.01);

            BRepPrimAPI_MakeSphere sphere(p, radius);
            Handle(AIS_Shape) redShape = new AIS_Shape(sphere.Shape());
            redShape->SetColor(Quantity_Color(Quantity_NOC_RED));
            redShape->SetDisplayMode(AIS_Shaded);
            m_context->Display(redShape, Standard_False);
            m_context->SetZLayer(redShape, Graphic3d_ZLayerId_Top);
            m_context->Deactivate(redShape);
            m_redShapes.append(redShape);
        }
        else if (type == TopAbs_SOLID) {
            // 实体：整体半透明红
            Handle(AIS_Shape) redShape = new AIS_Shape(shape);
            redShape->SetColor(Quantity_Color(Quantity_NOC_RED));
            redShape->SetTransparency(0.5);
            redShape->SetDisplayMode(AIS_Shaded);
            m_context->Display(redShape, Standard_False);
            m_context->SetZLayer(redShape, Graphic3d_ZLayerId_Top);
            m_context->Deactivate(redShape);
            m_redShapes.append(redShape);
        }
    } catch (Standard_Failure &e) {
        qDebug() << "[RED] 异常：" << e.GetMessageString();
    }
}

void OcctViewport::showTextLabel(const QString &text, const gp_Pnt &pos)
{
    if (text.isEmpty()) return;

    gp_Pnt safePos = pos;
    double maxCoord = 1e6;
    if (!std::isfinite(pos.X()) || !std::isfinite(pos.Y()) || !std::isfinite(pos.Z()) ||
        std::abs(pos.X()) > maxCoord || std::abs(pos.Y()) > maxCoord || std::abs(pos.Z()) > maxCoord)
    {
        safePos = gp_Pnt((m_bboxMin.x() + m_bboxMax.x()) * 0.5,
                         (m_bboxMin.y() + m_bboxMax.y()) * 0.5,
                         (m_bboxMin.z() + m_bboxMax.z()) * 0.5);
    }

    double size = (m_bboxMax - m_bboxMin).length();
    double height = 5.0;
    if (std::isfinite(size) && size > 1e-6 && size < 1e5)
        height = size * 0.02;

    try {
        Handle(AIS_TextLabel) label = new AIS_TextLabel();
        label->SetText(text.toStdString().c_str());
        label->SetPosition(safePos);
        label->SetHeight(height);

        Handle(Prs3d_Drawer) drawer = label->Attributes();
        if (!drawer.IsNull()) drawer->SetColor(Quantity_Color(Quantity_NOC_RED));

        label->SetZLayer(Graphic3d_ZLayerId_Top);
        m_context->Display(label, Standard_False);
        m_context->Deactivate(label);
        m_textLabels.append(label);
    } catch (Standard_Failure &e) {
        qDebug() << "[LABEL] 异常：" << e.GetMessageString();
    }
}

gp_Pnt OcctViewport::getFaceCenter(const TopoDS_Face &face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    double area = props.Mass();
    // 面积退化（零/非法）时 CentreOfMass() 内部除零，返回包围盒中心兜底
    if (!std::isfinite(area) || std::abs(area) < Precision::Confusion()) {
        return gp_Pnt((m_bboxMin.x() + m_bboxMax.x()) * 0.5,
                      (m_bboxMin.y() + m_bboxMax.y()) * 0.5,
                      (m_bboxMin.z() + m_bboxMax.z()) * 0.5);
    }
    return props.CentreOfMass();
}

void OcctViewport::addRecord(const QString &type, const QString &value, const QString &detail)
{
    if (!m_recordCallback) return;
    MeasureRecord rec;
    rec.type = type;
    rec.value = value;
    rec.detail = detail;
    rec.time = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    m_recordCallback(rec);
}

void OcctViewport::applyDisplayMode()
{
    if (!m_currentShape.IsNull()) {
        m_context->Remove(m_currentShape, Standard_False);
        switch (m_displayMode) {
        case 0: m_currentShape->SetDisplayMode(AIS_Shaded); m_currentShape->SetTransparency(0.0); break;
        case 1: m_currentShape->SetDisplayMode(AIS_WireFrame); m_currentShape->SetTransparency(0.0); break;
        case 2: m_currentShape->SetDisplayMode(AIS_Shaded); m_currentShape->SetTransparency(0.7); break;
        }
        m_context->Display(m_currentShape, Standard_False);
    }

    for (auto &ais : m_pathToShape) {
        if (ais.IsNull()) continue;
        m_context->Remove(ais, Standard_False);
        switch (m_displayMode) {
        case 0: ais->SetDisplayMode(AIS_Shaded); ais->SetTransparency(0.0); break;
        case 1: ais->SetDisplayMode(AIS_WireFrame); ais->SetTransparency(0.0); break;
        case 2: ais->SetDisplayMode(AIS_Shaded); ais->SetTransparency(0.7); break;
        }
        m_context->Display(ais, Standard_False);
    }
    m_view->Redraw();
}

bool OcctViewport::getFaceNormal(const TopoDS_Face &face, gp_Dir &normal)
{
    try {
        BRepAdaptor_Surface surf(face);
        double u = (surf.FirstUParameter() + surf.LastUParameter()) / 2;
        double v = (surf.FirstVParameter() + surf.LastVParameter()) / 2;
        BRepLProp_SLProps props(surf, u, v, 1, 1e-6);
        if (!props.IsNormalDefined()) return false;
        gp_Dir n = props.Normal();
        // 显式归一化：BRepLProp_SLProps::Normal() 理论上已是单位向量，
        // 但数值积分在退化点可能给出接近零或未归一化的向量，
        // 直接拿它算夹角会得到 NaN 或错误角度。
        double mag = std::sqrt(n.X()*n.X() + n.Y()*n.Y() + n.Z()*n.Z());
        if (!std::isfinite(mag) || mag < 1e-12) return false;
        normal = gp_Dir(n.X() / mag, n.Y() / mag, n.Z() / mag);
        return true;
    } catch (Standard_Failure &) { return false; }
}

bool OcctViewport::getFaceRadius(const TopoDS_Face &face, double &radius, QString &typeName)
{
    try {
        BRepAdaptor_Surface surf(face);
        GeomAbs_SurfaceType type = surf.GetType();
        switch (type) {
        case GeomAbs_Cylinder: radius = surf.Cylinder().Radius(); typeName = QStringLiteral("圆柱"); return true;
        case GeomAbs_Sphere:   radius = surf.Sphere().Radius();   typeName = QStringLiteral("球面"); return true;
        case GeomAbs_Cone: {
            double v = (surf.FirstVParameter() + surf.LastVParameter()) / 2;
            radius = surf.Cone().RefRadius() + v * std::tan(surf.Cone().SemiAngle());
            typeName = QStringLiteral("圆锥（中截面）");
            return true;
        }
        case GeomAbs_Torus:    radius = surf.Torus().MajorRadius(); typeName = QStringLiteral("圆环（主半径）"); return true;
        default: return false;
        }
    } catch (Standard_Failure &) { return false; }
}

void OcctViewport::doMeasurePick(const QPoint &pos)
{
    qreal dpr = devicePixelRatio();
    m_context->MoveTo(pos.x() * dpr, pos.y() * dpr, m_view, Standard_True);

    if (!m_context->HasDetected() || m_context->DetectedOwner().IsNull()) {
        if (m_messageCallback)
            m_messageCallback(QStringLiteral("未选中任何对象，请点击模型"));
        return;
    }

    const Handle(AIS_InteractiveObject) detected = m_context->DetectedInteractive();
    if (detected.IsNull() || detected->Type() != AIS_KindOfInteractive_Shape) {
        if (m_messageCallback)
            m_messageCallback(QStringLiteral("未选中任何对象，请点击模型"));
        return;
    }

    TopoDS_Shape picked;
    try { picked = m_context->DetectedShape(); }
    catch (Standard_Failure &) { return; }
    if (picked.IsNull()) return;

    try {
        switch (m_measureMode) {
        case MeasureMode::Distance:
            if (picked.ShapeType() == TopAbs_FACE) handleDistance(picked);
            break;
        case MeasureMode::Angle:
            if (picked.ShapeType() == TopAbs_FACE) handleAngle(TopoDS::Face(picked));
            break;
        case MeasureMode::Radius:
            if (picked.ShapeType() == TopAbs_FACE) handleRadius(TopoDS::Face(picked));
            break;
        case MeasureMode::PointToPoint:
            if (picked.ShapeType() == TopAbs_VERTEX) handlePointToPoint(picked);
            break;
        case MeasureMode::Volume:
            if (picked.ShapeType() == TopAbs_SOLID) handleVolume(picked);
            break;
        default: break;
        }
    } catch (Standard_Failure &e) {
        if (m_messageCallback)
            m_messageCallback(QStringLiteral("测量失败：%1").arg(QString::fromUtf8(e.GetMessageString())));
        resetMeasureState();
    }
    m_view->Redraw();
}

// ---- 面到面距离 ----
void OcctViewport::handleDistance(const TopoDS_Shape &picked)
{
    if (m_measureFirst.IsNull()) {
        m_measureFirst = picked;
        showRedShape(picked);
        if (m_messageCallback) m_messageCallback(QStringLiteral("已选第一个面（红色），请点击第二个面"));
    } else {
        if (picked.TShape() == m_measureFirst.TShape()) {
            if (m_messageCallback) m_messageCallback(QStringLiteral("这是同一个面，请点击另一个面"));
            return;
        }
        try {
            BRepExtrema_DistShapeShape dist(m_measureFirst, picked);
            dist.Perform();
            if (dist.IsDone() && dist.NbSolution() > 0) {
                double d = dist.Value();
                QString valueStr = QStringLiteral("%1 mm").arg(d, 0, 'f', 3);
                gp_Pnt p1 = dist.PointOnShape1(1);
                gp_Pnt p2 = dist.PointOnShape2(1);
                gp_Pnt mid((p1.X()+p2.X())/2, (p1.Y()+p2.Y())/2, (p1.Z()+p2.Z())/2);
                showTextLabel(valueStr, mid);
                showRedShape(picked);
                if (m_messageCallback) m_messageCallback(QStringLiteral("距离 = %1").arg(valueStr));
                addRecord(QStringLiteral("距离"), valueStr, QStringLiteral("面到面"));
            }
        } catch (Standard_Failure &) {}
        resetMeasureState();
    }
}

// ---- 面到面角度 ----
void OcctViewport::handleAngle(const TopoDS_Face &face)
{
    gp_Dir normal;
    if (!getFaceNormal(face, normal)) {
        if (m_messageCallback) m_messageCallback(QStringLiteral("无法获取该面的法向量"));
        return;
    }

    if (m_measureFirst.IsNull()) {
        m_measureFirst = face;
        m_measureFirstNormal = normal;
        m_measureFirstCenter = getFaceCenter(face);
        showRedShape(face);
        if (m_messageCallback) m_messageCallback(QStringLiteral("已选第一个面（红色），请点击第二个面"));
    } else {
        if (face.TShape() == m_measureFirst.TShape()) return;
        double angleRad = m_measureFirstNormal.Angle(normal);
        double angleDeg = angleRad * 180.0 / M_PI;
        double planeAngle = std::min(angleDeg, 180.0 - angleDeg);
        QString valueStr = QStringLiteral("%1°").arg(planeAngle, 0, 'f', 3);
        gp_Pnt center2 = getFaceCenter(face);
        gp_Pnt mid((m_measureFirstCenter.X()+center2.X())/2,
                   (m_measureFirstCenter.Y()+center2.Y())/2,
                   (m_measureFirstCenter.Z()+center2.Z())/2);
        showTextLabel(valueStr, mid);
        showRedShape(face);
        if (m_messageCallback) m_messageCallback(QStringLiteral("夹角 = %1").arg(valueStr));
        addRecord(QStringLiteral("角度"), valueStr, QStringLiteral("法向量夹角 %1°").arg(angleDeg, 0, 'f', 2));
        resetMeasureState();
    }
}

// ---- 半径 ----
void OcctViewport::handleRadius(const TopoDS_Face &face)
{
    double radius = 0;
    QString typeName;
    if (!getFaceRadius(face, radius, typeName)) {
        if (m_messageCallback) m_messageCallback(QStringLiteral("该面不是圆柱/球/圆锥/圆环"));
        return;
    }
    showRedShape(face);
    QString valueStr = QStringLiteral("%1 mm").arg(radius, 0, 'f', 3);
    gp_Pnt center = getFaceCenter(face);
    showTextLabel(valueStr, center);

    if (m_messageCallback)
        m_messageCallback(QStringLiteral("%1半径 = %2（直径 = %3 mm）")
                              .arg(typeName)
                              .arg(valueStr)
                              .arg(radius * 2, 0, 'f', 3));

    addRecord(QStringLiteral("半径"), valueStr,
              QStringLiteral("%1，直径 %2 mm").arg(typeName).arg(radius * 2, 0, 'f', 3));

    resetMeasureState();
}

// ---- 点到点距离 ----
void OcctViewport::handlePointToPoint(const TopoDS_Shape &picked)
{
    if (m_measureFirst.IsNull()) {
        m_measureFirst = picked;
        showRedShape(picked);
        if (m_messageCallback) m_messageCallback(QStringLiteral("已选第一个顶点（红色），请点击第二个顶点"));
    } else {
        // 用坐标 + 容差判断是否同一个顶点，而不是比较 TShape 指针：
        // 两个不同顶点可能共享同一个 TShape（拓扑共享），而两个几何重合
        // 的独立顶点则 TShape 不同。坐标比较才符合"点到点"的语义。
        gp_Pnt v1 = BRep_Tool::Pnt(TopoDS::Vertex(m_measureFirst));
        gp_Pnt v2 = BRep_Tool::Pnt(TopoDS::Vertex(picked));
        if (v1.Distance(v2) <= Precision::Confusion()) {
            if (m_messageCallback) m_messageCallback(QStringLiteral("这是同一个顶点，请点击另一个"));
            return;
        }

        double d = v1.Distance(v2);

        QString valueStr = QStringLiteral("%1 mm").arg(d, 0, 'f', 3);

        gp_Pnt mid((v1.X()+v2.X())/2, (v1.Y()+v2.Y())/2, (v1.Z()+v2.Z())/2);
        showTextLabel(valueStr, mid);

        // 两个顶点都保留红色
        showRedShape(picked);
        showRedShape(m_measureFirst);

        if (m_messageCallback) m_messageCallback(QStringLiteral("点到点距离 = %1").arg(valueStr));
        addRecord(QStringLiteral("点到点"), valueStr, QStringLiteral("顶点到顶点"));
        resetMeasureState();
    }
}

// ---- 体积 + 表面积 ----
void OcctViewport::handleVolume(const TopoDS_Shape &picked)
{
    try {
        GProp_GProps volProps;
        BRepGProp::VolumeProperties(picked, volProps);
        double volume = volProps.Mass();

        // 体积为零/非法时（非闭合实体、退化形状），Mass() 可能返回 0 或 NaN，
        // 此时 CentreOfMass() 是未定义的（内部除以零）。先做保护。
        if (!std::isfinite(volume) || std::abs(volume) < Precision::Confusion()) {
            if (m_messageCallback)
                m_messageCallback(QStringLiteral("该实体无有效体积（可能非闭合）"));
            return;
        }

        GProp_GProps surfProps;
        BRepGProp::SurfaceProperties(picked, surfProps);
        double area = surfProps.Mass();

        // 体积：mm³ → cm³ 也显示一下（1 cm³ = 1000 mm³）
        double volumeCm3 = volume / 1000.0;

        QString volStr = QStringLiteral("%1 mm³").arg(volume, 0, 'f', 3);
        QString areaStr = QStringLiteral("%1 mm²").arg(area, 0, 'f', 3);

        showRedShape(picked);

        gp_Pnt center = volProps.CentreOfMass();
        showTextLabel(QStringLiteral("V=%1 mm³").arg(volume, 0, 'f', 1), center);

        if (m_messageCallback) {
            m_messageCallback(QStringLiteral("体积 = %1（%2 cm³）  表面积 = %3")
                                  .arg(volStr).arg(volumeCm3, 0, 'f', 3).arg(areaStr));
        }

        addRecord(QStringLiteral("体积"), volStr,
                  QStringLiteral("表面积 %1").arg(areaStr));
        addRecord(QStringLiteral("表面积"), areaStr,
                  QStringLiteral("体积 %1").arg(volStr));

        qDebug() << "[MEASURE] 体积 =" << volume << " mm³, 表面积 =" << area << " mm²";
    } catch (Standard_Failure &e) {
        qDebug() << "[MEASURE] 体积计算异常：" << e.GetMessageString();
        if (m_messageCallback) m_messageCallback(QStringLiteral("体积计算失败"));
    }

    resetMeasureState();
}

void OcctViewport::initClipPlane()
{
    m_clipPlane = new Graphic3d_ClipPlane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)));
    m_clipPlane->SetOn(false);
    m_clipPlane->SetCapping(true);
    m_clipPlane->SetCappingColor(Quantity_Color(Quantity_NOC_RED));
    m_view->AddClipPlane(m_clipPlane);
}

void OcctViewport::updateBoundingBox(const TopoDS_Shape &shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) {
        m_bboxMin = QVector3D(-100, -100, -100);
        m_bboxMax = QVector3D( 100,  100,  100);
        return;
    }
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    m_bboxMin = QVector3D(xmin, ymin, zmin);
    m_bboxMax = QVector3D(xmax, ymax, zmax);
}

void OcctViewport::refreshClipPlane()
{
    if (m_clipPlane.IsNull()) return;
    gp_Dir dir;
    double axisMin, axisMax;
    switch (m_clipAxis) {
    case 0: dir = gp_Dir(1, 0, 0); axisMin = m_bboxMin.x(); axisMax = m_bboxMax.x(); break;
    case 1: dir = gp_Dir(0, 1, 0); axisMin = m_bboxMin.y(); axisMax = m_bboxMax.y(); break;
    default:dir = gp_Dir(0, 0, 1); axisMin = m_bboxMin.z(); axisMax = m_bboxMax.z(); break;
    }
    double t = (m_clipPercent + 100) / 200.0;
    double pos = axisMin + t * (axisMax - axisMin);
    gp_Pnt pt(pos * (m_clipAxis == 0 ? 1 : 0),
              pos * (m_clipAxis == 1 ? 1 : 0),
              pos * (m_clipAxis == 2 ? 1 : 0));
    m_clipPlane->SetEquation(gp_Pln(pt, dir));
}

// ============================================================
// 自检：无需鼠标，在视口内扫描像素位置并走完整的拾取/测量流程，
// 用于自动化验证 5 种测量模式（--selftest-measure）。
// 返回若干行日志，成功行以 "[SELFTEST-OK]" 开头，失败行以 "[SELFTEST-FAIL]" 开头。
// ============================================================
QStringList OcctViewport::measureSelfTest()
{
    QStringList log;
    if (m_view.IsNull() || m_context.IsNull() || m_pathToShape.isEmpty()) {
        log << QStringLiteral("[SELFTEST-FAIL] 没有已加载的模型");
        return log;
    }
    if (width() < 100 || height() < 100) {
        log << QStringLiteral("[SELFTEST-FAIL] 视口尺寸异常 %1x%2").arg(width()).arg(height());
        return log;
    }

    // 临时接管回调，收集测量结果
    const auto savedMessage = m_messageCallback;
    const auto savedRecord  = m_recordCallback;
    QStringList records;
    m_messageCallback = [](const QString &) {};
    m_recordCallback  = [&records](const MeasureRecord &r) {
        records << QStringLiteral("%1 = %2 (%3)").arg(r.type, r.value, r.detail);
    };

    const qreal dpr = devicePixelRatio();
    auto detectAt = [this, dpr](const QPoint &p, TopoDS_Shape &out) -> bool {
        try {
            m_context->MoveTo(p.x() * dpr, p.y() * dpr, m_view, Standard_False);
            if (!m_context->HasDetected()) return false;
            out = m_context->DetectedShape();
        } catch (Standard_Failure &) {
            return false;
        }
        return !out.IsNull();
    };

    // 按网格扫描，收集指定拓扑类型、互不相同的可拾取位置
    auto pickPositions = [this, &detectAt](TopAbs_ShapeEnum want, int need) {
        QVector<QPoint> positions;
        QVector<TopoDS_Shape> shapes;
        const int nx = 11, ny = 11;
        for (int iy = 0; iy < ny && positions.size() < need; ++iy) {
            for (int ix = 0; ix < nx && positions.size() < need; ++ix) {
                const QPoint p(width()  * (2 * ix + 1) / (2 * nx),
                               height() * (2 * iy + 1) / (2 * ny));
                TopoDS_Shape s;
                if (!detectAt(p, s) || s.ShapeType() != want) continue;
                bool duplicated = false;
                for (const TopoDS_Shape &prev : shapes) {
                    if (prev.TShape() == s.TShape()) { duplicated = true; break; }
                }
                if (duplicated) continue;
                shapes.append(s);
                positions.append(p);
            }
        }
        return positions;
    };

    auto runMode = [&](const QString &label, MeasureMode mode,
                       TopAbs_ShapeEnum want, int count) {
        records.clear();
        setMeasureMode(mode);
        const QVector<QPoint> pos = pickPositions(want, count);
        if (pos.size() < count) {
            log << QStringLiteral("[SELFTEST-FAIL] %1：只找到 %2 个可拾取元素（需要 %3）")
                       .arg(label).arg(pos.size()).arg(count);
            return false;
        }
        for (const QPoint &p : pos) doMeasurePick(p);
        if (records.isEmpty()) {
            log << QStringLiteral("[SELFTEST-FAIL] %1：拾取成功但没有产生测量结果").arg(label);
            return false;
        }
        for (const QString &r : records)
            log << QStringLiteral("[SELFTEST-OK] %1 -> %2").arg(label, r);
        return true;
    };

    int passed = 0;
    if (runMode(QStringLiteral("距离"), MeasureMode::Distance, TopAbs_FACE, 2)) ++passed;
    if (runMode(QStringLiteral("角度"), MeasureMode::Angle, TopAbs_FACE, 2)) ++passed;
    if (runMode(QStringLiteral("点到点"), MeasureMode::PointToPoint, TopAbs_VERTEX, 2)) ++passed;
    if (runMode(QStringLiteral("体积/表面积"), MeasureMode::Volume, TopAbs_SOLID, 1)) ++passed;

    // 半径：需要圆柱/球/圆锥/圆环面，逐点挑第一个能取到半径的面
    {
        records.clear();
        setMeasureMode(MeasureMode::Radius);
        QPoint hitPos;
        bool found = false;
        for (int iy = 0; iy < 11 && !found; ++iy) {
            for (int ix = 0; ix < 11 && !found; ++ix) {
                const QPoint p(width()  * (2 * ix + 1) / (2 * 11),
                               height() * (2 * iy + 1) / (2 * 11));
                TopoDS_Shape s;
                if (!detectAt(p, s) || s.ShapeType() != TopAbs_FACE) continue;
                try {
                    double radius = 0.0;
                    QString typeName;
                    if (getFaceRadius(TopoDS::Face(s), radius, typeName)) {
                        hitPos = p;
                        found = true;
                    }
                } catch (Standard_Failure &) {
                }
            }
        }
        if (!found) {
            log << QStringLiteral("[SELFTEST-FAIL] 半径：模型中没有可测量半径的面");
        } else {
            doMeasurePick(hitPos);
            if (records.isEmpty())
                log << QStringLiteral("[SELFTEST-FAIL] 半径：拾取后没有产生测量结果");
            else {
                for (const QString &r : records)
                    log << QStringLiteral("[SELFTEST-OK] 半径 -> %1").arg(r);
                ++passed;
            }
        }
    }

    log << QStringLiteral("[SELFTEST] 通过 %1/5 项测量").arg(passed);

    setMeasureMode(MeasureMode::None);
    m_messageCallback = savedMessage;
    m_recordCallback  = savedRecord;
    m_view->Redraw();
    return log;
}
