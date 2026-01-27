#ifndef DESKTOPHELPER_H
#define DESKTOPHELPER_H

#include <QString>
#include <QPoint>

class DesktopHelper
{
public:
    // 获取桌面 SysListView32 句柄
    static void* getDesktopListView();

    // 获取指定文件的桌面图标位置
    static QPoint getIconPosition(const QString &fileName);

    // 设置指定文件的桌面图标位置（retryCount 用于内部重试）
    static void setIconPosition(const QString &fileName, const QPoint &pos, int retryCount = 0);

    // 刷新桌面
    static void refreshDesktop();
    
    // 通知特定文件被移除（更快的刷新方式）
    static void notifyFileRemoved(const QString &filePath);
    
    // 通知特定文件被添加
    static void notifyFileAdded(const QString &filePath);
};

#endif // DESKTOPHELPER_H
