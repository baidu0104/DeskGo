#include <QApplication>
#include <QMessageBox>
#include "src/core/fencemanager.h"

int main(int argc, char *argv[])
{
    // 启用高 DPI 缩放，解决 2K/4K 屏幕文字模糊问题
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication a(argc, argv);
    
    // 设置应用程序信息
    a.setApplicationName("DeskGo");
    a.setApplicationVersion("1.0.0");
    a.setOrganizationName("DeskGo");
    // 设置不随最后一个窗口关闭而退出（因为我们用托盘）
    a.setQuitOnLastWindowClosed(false);

    // 初始化围栏管理器
    FenceManager::instance()->initialize();

    int ret = a.exec();
    
    // 显式清理资源，确保在 QApplication 析构前完成
    FenceManager::instance()->shutdown();
    
    return ret;
}
