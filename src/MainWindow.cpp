#include "MainWindow.h"
#include "OcctViewport.h"

#include <QApplication>
#include <QFileDialog>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QToolBar>
#include <QSlider>
#include <QLabel>
#include <QStatusBar>
#include <QMessageBox>
#include <QDebug>
#include <QDockWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFile>
#include <QTextStream>
#include <QImage>
#include <QPainter>
#include <QPixmap>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("OCCT Viewer - XCAF 装配体"));
    resize(1500, 850);

    m_viewport = new OcctViewport(this);
    setCentralWidget(m_viewport);

    statusBar()->showMessage(QStringLiteral("就绪"));

    m_viewport->setMessageCallback([this](const QString &msg) { statusBar()->showMessage(msg); });
    m_viewport->setRecordCallback([this](const MeasureRecord &rec) { addRecordToTable(rec); });
    m_viewport->setPartsCallback([this](const QVector<PartInfo> &parts) { buildPartsTree(parts); });

    createMenus();
    createToolBar();
    createPartsDock();
    createMeasureHistoryDock();

    // ============================================================
    // 启动时自动加载内置装配示例：untitled2.exe [可选: 其它 .step 路径]
    // 示例模型已通过 Qt 资源系统打包进 exe（:/resources/samples/as1_motor.step），
    // 因此 dist/ 目录可直接双击运行，无需外部模型文件；带参数时以参数为准。
    // ============================================================
    const QString AutoloadStepFile = QStringLiteral(":/resources/samples/as1_motor.step");

    QString autoStep = AutoloadStepFile;
    // 命令行：untitled2.exe [某个 .step 路径]；--screenshot <png> / --smoke 等
    // 开关不是模型路径，解析时跳过开关本身及其取值。
    const QStringList args = qApp->arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == QLatin1String("--screenshot") || a == QLatin1String("--view")) { ++i; continue; }
        if (a.startsWith(QLatin1Char('-'))) continue;
        autoStep = a;
        break;
    }

    if (QFile::exists(autoStep)) {
        qDebug() << "[AUTOLOAD] 尝试加载：" << autoStep;
        if (m_viewport->loadStepXCAF(autoStep)) {
            m_loadOk = true;
            statusBar()->showMessage(QStringLiteral("加载成功：%1").arg(autoStep));
        }
    }
}

void MainWindow::setStandardView(int viewType)
{
    if (m_viewport) m_viewport->setStandardView(viewType);
}

// ============================================================
// 测量自检：转发到视口，逐行日志由调用方打印（--selftest-measure）
// ============================================================
QStringList MainWindow::runMeasureSelfTest()
{
    if (!m_viewport) return QStringList() << QStringLiteral("[SELFTEST-FAIL] 视口未初始化");
    return m_viewport->measureSelfTest();
}

// ============================================================
// 截图：Qt 的 QWidget::grab() 能抓到完整 UI（菜单/工具栏/停靠面板），
// 但抓不到 OCCT 直接绘制到原生窗口的 3D 内容，因此把 3D 帧缓冲
// 单独导出后，按视口在窗口中的位置合成回去。
// ============================================================
void MainWindow::saveScreenshot(const QString &fileName)
{
    const QPixmap shot = grab();
    QImage ui = shot.toImage();
    if (ui.isNull()) return;
    // grab() 得到的 QImage 带 DPR 元数据，QPainter 会再按 DPR 缩放一次；
    // 这里统一改成「1 像素 = 1 图像像素」，后面按设备像素坐标合成。
    const qreal dpr = shot.devicePixelRatio();
    ui.setDevicePixelRatio(1.0);

    const QImage scene = m_viewport ? m_viewport->dumpImage() : QImage();
    if (!scene.isNull()) {
        const QRect vp = m_viewport->geometry();
        const QRect target(qRound(vp.x() * dpr), qRound(vp.y() * dpr),
                           qRound(vp.width() * dpr), qRound(vp.height() * dpr));
        qDebug() << "[SCREENSHOT] window" << ui.size() << "dpr" << dpr
                 << "viewport geo" << vp << "scene" << scene.size() << "target" << target;
        QPainter p(&ui);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(target, scene);
    }

    if (!ui.save(fileName, "PNG"))
        qWarning() << "[SCREENSHOT] 保存失败：" << fileName;
    else
        qDebug() << "[SCREENSHOT] 已保存：" << fileName << ui.width() << "x" << ui.height();
}

void MainWindow::createPartsDock()
{
    QDockWidget *dock = new QDockWidget(QStringLiteral("零件树"), this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *panel = new QWidget(dock);
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);

    m_tree = new QTreeWidget(panel);
    m_tree->setHeaderLabel(QStringLiteral("装配结构"));
    m_tree->setColumnCount(1);
    layout->addWidget(m_tree);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnShowAll = new QPushButton(QStringLiteral("全部显示"), panel);
    QPushButton *btnHideAll = new QPushButton(QStringLiteral("全部隐藏"), panel);
    btnLayout->addWidget(btnShowAll);
    btnLayout->addWidget(btnHideAll);
    layout->addLayout(btnLayout);

    connect(btnShowAll, &QPushButton::clicked, this, &MainWindow::showAllParts);
    connect(btnHideAll, &QPushButton::clicked, this, &MainWindow::hideAllParts);
    connect(m_tree, &QTreeWidget::itemChanged, this, &MainWindow::onTreeItemChanged);
    connect(m_tree, &QTreeWidget::itemClicked, this, &MainWindow::onTreeItemClicked);

    dock->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::buildPartsTree(const QVector<PartInfo> &parts)
{
    m_tree->blockSignals(true);
    m_tree->clear();
    m_treeItems.clear();

    QVector<QTreeWidgetItem*> items(parts.size(), nullptr);

    for (int i = 0; i < parts.size(); ++i) {
        const PartInfo &info = parts[i];
        QTreeWidgetItem *item = nullptr;
        if (info.parentIdx >= 0 && info.parentIdx < items.size() && items[info.parentIdx])
            item = new QTreeWidgetItem(items[info.parentIdx]);
        else
            item = new QTreeWidgetItem(m_tree);

        item->setText(0, info.name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
        item->setData(0, Qt::UserRole, info.path);
        item->setData(0, Qt::UserRole + 1, info.isAssembly);

        if (info.isAssembly) item->setForeground(0, QBrush(QColor(120, 120, 120)));

        items[i] = item;
        m_treeItems.append(item);
    }

    m_tree->expandAll();
    m_tree->blockSignals(false);
}

void MainWindow::onTreeItemChanged(QTreeWidgetItem *item, int column)
{
    if (column != 0) return;
    bool isAssembly = item->data(0, Qt::UserRole + 1).toBool();
    bool checked = (item->checkState(0) == Qt::Checked);

    if (isAssembly) {
        for (int i = 0; i < item->childCount(); ++i)
            item->child(i)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
    } else {
        QString path = item->data(0, Qt::UserRole).toString();
        m_viewport->setPartVisible(path, checked);
    }
}

void MainWindow::onTreeItemClicked(QTreeWidgetItem *item, int)
{
    bool isAssembly = item->data(0, Qt::UserRole + 1).toBool();
    if (!isAssembly) {
        QString path = item->data(0, Qt::UserRole).toString();
        m_viewport->highlightPart(path);
    }
}

void MainWindow::showAllParts()
{
    m_tree->blockSignals(true);
    for (auto *item : m_treeItems) item->setCheckState(0, Qt::Checked);
    m_tree->blockSignals(false);
    for (auto *item : m_treeItems) {
        if (!item->data(0, Qt::UserRole + 1).toBool())
            m_viewport->setPartVisible(item->data(0, Qt::UserRole).toString(), true);
    }
}

void MainWindow::hideAllParts()
{
    m_tree->blockSignals(true);
    for (auto *item : m_treeItems) item->setCheckState(0, Qt::Unchecked);
    m_tree->blockSignals(false);
    for (auto *item : m_treeItems) {
        if (!item->data(0, Qt::UserRole + 1).toBool())
            m_viewport->setPartVisible(item->data(0, Qt::UserRole).toString(), false);
    }
}

void MainWindow::createMeasureHistoryDock()
{
    QDockWidget *dock = new QDockWidget(QStringLiteral("测量历史"), this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *panel = new QWidget(dock);
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);

    m_table = new QTableWidget(panel);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("序号"), QStringLiteral("类型"),
        QStringLiteral("测量值"), QStringLiteral("详情"), QStringLiteral("时间")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setColumnWidth(0, 40);
    m_table->setColumnWidth(1, 60);
    m_table->setColumnWidth(2, 110);
    m_table->setColumnWidth(3, 140);
    layout->addWidget(m_table);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnExport = new QPushButton(QStringLiteral("导出 CSV"), panel);
    QPushButton *btnClear  = new QPushButton(QStringLiteral("清空"), panel);
    btnLayout->addWidget(btnExport);
    btnLayout->addWidget(btnClear);
    layout->addLayout(btnLayout);

    connect(btnExport, &QPushButton::clicked, this, &MainWindow::exportCSV);
    connect(btnClear, &QPushButton::clicked, this, &MainWindow::clearHistory);

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::addRecordToTable(const MeasureRecord &rec)
{
    int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
    m_table->setItem(row, 1, new QTableWidgetItem(rec.type));
    m_table->setItem(row, 2, new QTableWidgetItem(rec.value));
    m_table->setItem(row, 3, new QTableWidgetItem(rec.detail));
    m_table->setItem(row, 4, new QTableWidgetItem(rec.time));
    m_table->scrollToBottom();
}

void MainWindow::exportCSV()
{
    if (m_table->rowCount() == 0) {
        QMessageBox::information(this, QStringLiteral("导出"), QStringLiteral("没有记录"));
        return;
    }
    QString fileName = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出"), QStringLiteral("measure_history.csv"),
        QStringLiteral("CSV 文件 (*.csv)"));
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "\xEF\xBB\xBF";
    out << "序号,类型,测量值,详情,时间\n";

    for (int r = 0; r < m_table->rowCount(); ++r) {
        for (int c = 0; c < m_table->columnCount(); ++c) {
            if (c > 0) out << ",";
            QTableWidgetItem *item = m_table->item(r, c);
            QString text = item ? item->text() : QString();
            if (text.contains(',') || text.contains('"')) {
                text.replace("\"", "\"\"");
                out << "\"" << text << "\"";
            } else out << text;
        }
        out << "\n";
    }
    file.close();
    statusBar()->showMessage(QStringLiteral("已导出"));
}

void MainWindow::clearHistory()
{
    if (m_table->rowCount() == 0) return;
    if (QMessageBox::question(this, QStringLiteral("清空"),
                              QStringLiteral("确定清空所有记录吗？")) == QMessageBox::Yes) {
        m_table->setRowCount(0);
        m_viewport->clearLabelsOnly();
    }
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    QAction *openAction = fileMenu->addAction(QStringLiteral("打开 STEP 文件(&O)..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openStepFile);

    fileMenu->addSeparator();
    QAction *exportAction = fileMenu->addAction(QStringLiteral("导出测量记录(&E)..."));
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportCSV);

    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction(QStringLiteral("退出(&Q)"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));

    QAction *fitAction = viewMenu->addAction(QStringLiteral("适应窗口(&F)"));
    fitAction->setShortcut(Qt::Key_F);
    connect(fitAction, &QAction::triggered, this, [this]() { m_viewport->fitAll(); });

    QAction *boxAction = viewMenu->addAction(QStringLiteral("显示立方体(&B)"));
    connect(boxAction, &QAction::triggered, this, [this]() { m_viewport->showBox(); });

    QAction *cylAction = viewMenu->addAction(QStringLiteral("显示圆柱(&C)"));
    connect(cylAction, &QAction::triggered, this, [this]() { m_viewport->showCylinder(); });

    viewMenu->addSeparator();

    // ---- 示例模型（内嵌资源，无需外部文件） ----
    QMenu *stepMenu = viewMenu->addMenu(QStringLiteral("示例模型(&S)"));

    struct { const char *name; const char *path; } stepItems[] = {
        {"as1_motor.step（电机装配，AP214，39 零件，内嵌资源）",
         ":/resources/samples/as1_motor.step"},
    };

    for (auto &s : stepItems) {
        const QString path = QString::fromUtf8(s.path);
        QAction *a = stepMenu->addAction(QString::fromUtf8(s.name));
        connect(a, &QAction::triggered, this, [this, path]() {
            if (!QFile::exists(path)) {
                QMessageBox::warning(this, QStringLiteral("资源缺失"),
                                     QStringLiteral("找不到内嵌资源：\n%1\n\n"
                                                    "请确认 resources.qrc 已编译进可执行文件。").arg(path));
                return;
            }
            statusBar()->showMessage(QStringLiteral("正在加载..."));
            QApplication::processEvents();
            if (m_viewport->loadStepXCAF(path))
                statusBar()->showMessage(QStringLiteral("加载成功：%1").arg(path));
            else
                QMessageBox::warning(this, QStringLiteral("加载失败"),
                                     QStringLiteral("无法加载：\n%1").arg(path));
        });
    }

    stepMenu->addSeparator();
    QAction *stepOpenAction = stepMenu->addAction(QStringLiteral("浏览其它 STEP 文件(&B)..."));
    connect(stepOpenAction, &QAction::triggered, this, &MainWindow::openStepFile);

    QMenu *stdViewMenu = viewMenu->addMenu(QStringLiteral("标准视角"));
    struct { const char *name; int id; const char *key; } views[] = {
                  {"前视图", 0, "1"}, {"后视图", 1, "2"}, {"右视图", 2, "3"},
                  {"左视图", 3, "4"}, {"顶视图", 4, "5"}, {"底视图", 5, "6"},
                  {"等轴测", 6, "0"},
                  };
    for (auto &v : views) {
        QAction *a = stdViewMenu->addAction(QString::fromUtf8(v.name));
        a->setShortcut(QKeySequence(QString::fromUtf8(v.key)));
        int id = v.id;
        connect(a, &QAction::triggered, this, [this, id]() { m_viewport->setStandardView(id); });
    }

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    QAction *aboutAction = helpMenu->addAction(QStringLiteral("关于(&A)..."));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QStringLiteral("关于"),
                           QStringLiteral("OCCT Viewer - XCAF 装配体查看器\n\n"
                                          "测量：距离、角度、半径、点到点、体积/表面积\n"
                                          "零件树、剖切、3D 标注、测量历史"));
    });
}

void MainWindow::createToolBar()
{
    QToolBar *tb = addToolBar(QStringLiteral("工具"));
    tb->setMovable(false);

    tb->addWidget(new QLabel(QStringLiteral(" 显示: ")));
    QActionGroup *dispGroup = new QActionGroup(this);

    QAction *dSolid = tb->addAction(QStringLiteral("实体"));
    dSolid->setCheckable(true); dSolid->setChecked(true);
    dispGroup->addAction(dSolid);
    connect(dSolid, &QAction::triggered, this, [this]() { m_viewport->setDisplayMode(0); });

    QAction *dWire = tb->addAction(QStringLiteral("线框"));
    dWire->setCheckable(true); dispGroup->addAction(dWire);
    connect(dWire, &QAction::triggered, this, [this]() { m_viewport->setDisplayMode(1); });

    QAction *dTrans = tb->addAction(QStringLiteral("半透明"));
    dTrans->setCheckable(true); dispGroup->addAction(dTrans);
    connect(dTrans, &QAction::triggered, this, [this]() { m_viewport->setDisplayMode(2); });

    tb->addSeparator();
    tb->addWidget(new QLabel(QStringLiteral(" 视角: ")));
    const char *viewNames[] = {"前", "后", "右", "左", "顶", "底", "等轴测"};
    for (int i = 0; i < 7; ++i) {
        QAction *a = tb->addAction(QString::fromUtf8(viewNames[i]));
        int id = i;
        connect(a, &QAction::triggered, this, [this, id]() { m_viewport->setStandardView(id); });
    }

    tb->addSeparator();

    QActionGroup *measureGroup = new QActionGroup(this);

    QAction *measDist = tb->addAction(QStringLiteral("测距离"));
    measDist->setCheckable(true); measureGroup->addAction(measDist);
    connect(measDist, &QAction::toggled, this, [this](bool on) {
        m_viewport->setMeasureMode(on ? MeasureMode::Distance : MeasureMode::None);
    });

    QAction *measAngle = tb->addAction(QStringLiteral("测角度"));
    measAngle->setCheckable(true); measureGroup->addAction(measAngle);
    connect(measAngle, &QAction::toggled, this, [this](bool on) {
        m_viewport->setMeasureMode(on ? MeasureMode::Angle : MeasureMode::None);
    });

    QAction *measRadius = tb->addAction(QStringLiteral("测半径"));
    measRadius->setCheckable(true); measureGroup->addAction(measRadius);
    connect(measRadius, &QAction::toggled, this, [this](bool on) {
        m_viewport->setMeasureMode(on ? MeasureMode::Radius : MeasureMode::None);
    });

    QAction *measP2P = tb->addAction(QStringLiteral("点到点"));
    measP2P->setCheckable(true); measureGroup->addAction(measP2P);
    connect(measP2P, &QAction::toggled, this, [this](bool on) {
        m_viewport->setMeasureMode(on ? MeasureMode::PointToPoint : MeasureMode::None);
    });

    QAction *measVol = tb->addAction(QStringLiteral("体积/表面积"));
    measVol->setCheckable(true); measureGroup->addAction(measVol);
    connect(measVol, &QAction::toggled, this, [this](bool on) {
        m_viewport->setMeasureMode(on ? MeasureMode::Volume : MeasureMode::None);
    });

    QAction *clearAction = tb->addAction(QStringLiteral("清除标注"));
    connect(clearAction, &QAction::triggered, this, [this]() { m_viewport->clearMeasurement(); });

    tb->addSeparator();

    m_clipEnableAction = tb->addAction(QStringLiteral("启用剖切"));
    m_clipEnableAction->setCheckable(true);
    connect(m_clipEnableAction, &QAction::toggled, this, [this](bool on) {
        m_viewport->setClipEnabled(on);
    });

    tb->addSeparator();

    QActionGroup *axisGroup = new QActionGroup(this);
    QAction *ax = tb->addAction(QStringLiteral("X"));
    ax->setCheckable(true); ax->setChecked(true); axisGroup->addAction(ax);
    connect(ax, &QAction::triggered, this, [this]() { m_viewport->setClipAxis(0); });

    QAction *ay = tb->addAction(QStringLiteral("Y"));
    ay->setCheckable(true); axisGroup->addAction(ay);
    connect(ay, &QAction::triggered, this, [this]() { m_viewport->setClipAxis(1); });

    QAction *az = tb->addAction(QStringLiteral("Z"));
    az->setCheckable(true); axisGroup->addAction(az);
    connect(az, &QAction::triggered, this, [this]() { m_viewport->setClipAxis(2); });

    tb->addSeparator();
    tb->addWidget(new QLabel(QStringLiteral(" 位置: ")));
    QSlider *slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(-100, 100); slider->setValue(0); slider->setMinimumWidth(150);
    connect(slider, &QSlider::valueChanged, this, [this](int v) { m_viewport->setClipPosition(v); });
    tb->addWidget(slider);
}

void MainWindow::openStepFile()
{
    QString fileName = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开 STEP"), QString(),
        QStringLiteral("STEP 文件 (*.stp *.step);;所有文件 (*.*)"));
    if (fileName.isEmpty()) return;
    statusBar()->showMessage(QStringLiteral("正在加载..."));
    QApplication::processEvents();
    if (!m_viewport->loadStepXCAF(fileName))
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("无法加载"));
    else {
        m_loadOk = true;
        statusBar()->showMessage(QStringLiteral("加载成功"));
    }
}
