// ============================================================================
//  OCCT Viewer — 程序入口
//  Qt 6 + OpenCASCADE 3D CAD 装配体查看器
//
//  用法：
//    untitled2.exe                              启动并自动加载内置示例装配
//    untitled2.exe <file.step>                  启动并加载指定 STEP 文件
//    untitled2.exe --screenshot <out.png>       自动截图后退出（README/CI 用）
//    untitled2.exe --smoke [--screenshot ...]   自检：加载成功退出 0，否则 1
//    untitled2.exe --selftest-measure           测量自检：模拟拾取，验证 5 种测量
// ============================================================================
#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QTimer>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("OCCT Viewer"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setOrganizationName(QStringLiteral("OCCT Viewer"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/app.ico")));

    const QStringList args = app.arguments();
    const int shotIndex = args.indexOf(QStringLiteral("--screenshot"));
    const QString shotPath = (shotIndex >= 0 && shotIndex + 1 < args.size())
                                 ? args.at(shotIndex + 1) : QString();
    const bool smokeOnly = args.contains(QStringLiteral("--smoke"));
    const bool selfTestMeasure = args.contains(QStringLiteral("--selftest-measure"));

    // 可选：--view <0..6> 指定标准视角（6 = 等轴测），便于生成展示截图
    int viewMode = -1;
    const int viewIndex = args.indexOf(QStringLiteral("--view"));
    if (viewIndex >= 0 && viewIndex + 1 < args.size()) {
        bool ok = false;
        const int v = args.at(viewIndex + 1).toInt(&ok);
        if (ok && v >= 0 && v <= 6) viewMode = v;
    }

    MainWindow window;
    window.show();
    if (viewMode >= 0) window.setStandardView(viewMode);

    // 等窗口布局、OCCT 视口尺寸与首次渲染稳定后再截图
    if (!shotPath.isEmpty()) {
        QTimer::singleShot(1500, &app, [&window, shotPath]() {
            window.saveScreenshot(shotPath);
            window.close();
        });
    } else if (smokeOnly) {
        QTimer::singleShot(3000, &app, [&window]() { window.close(); });
    } else if (selfTestMeasure) {
        QTimer::singleShot(2000, &app, [&app, &window]() {
            const QStringList report = window.runMeasureSelfTest();
            for (const QString &line : report) qInfo().noquote() << line;
            const int okCount = report.filter(QStringLiteral("[SELFTEST-OK]")).size();
            app.exit(okCount >= 5 ? 0 : 2);
        });
    }

    const int rc = app.exec();
    if (smokeOnly && !shotPath.isEmpty()) return rc;
    if (smokeOnly) return window.wasLoadSuccessful() ? 0 : 1;
    return rc;
}