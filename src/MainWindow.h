#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "Types.h"

#include <QMainWindow>
#include <QStringList>
#include <QVector>
#include <QTreeWidgetItem>

class OcctViewport;
class QTableWidget;
class QTreeWidget;
class QAction;

// ============================================================
// 主窗口：菜单、工具栏、零件树、测量历史、剖切
// ============================================================
class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // 把整个窗口（UI + 3D 视口）渲染成一张 PNG，用于 README 截图 / CI
    void saveScreenshot(const QString &fileName);
    // 切换到标准视角（0..6，6 = 等轴测），截图/演示用
    void setStandardView(int viewType);
    // 最近一次 STEP 加载是否成功（--smoke 自检用）
    bool wasLoadSuccessful() const { return m_loadOk; }
    // 测量自检（--selftest-measure）：转发到视口，返回逐行日志
    QStringList runMeasureSelfTest();

private:
    void createPartsDock();
    void buildPartsTree(const QVector<PartInfo> &parts);
    void onTreeItemChanged(QTreeWidgetItem *item, int column);
    void onTreeItemClicked(QTreeWidgetItem *item, int);
    void showAllParts();
    void hideAllParts();

    void createMeasureHistoryDock();
    void addRecordToTable(const MeasureRecord &rec);
    void exportCSV();
    void clearHistory();

    void createMenus();
    void createToolBar();

private slots:
    void openStepFile();

private:
    OcctViewport *m_viewport = nullptr;
    QAction      *m_clipEnableAction = nullptr;
    QTableWidget *m_table = nullptr;
    QTreeWidget  *m_tree = nullptr;
    QVector<QTreeWidgetItem*> m_treeItems;
    bool m_loadOk = false;
};

#endif // MAINWINDOW_H
