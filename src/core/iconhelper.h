#ifndef ICONHELPER_H
#define ICONHELPER_H

#include <QString>
#include <QPixmap>

/**
 * @brief 图标处理助手
 * 提供系统图标提取、透明区域裁剪、路径转换等静态工具方法。
 */
class IconHelper {
public:
    // 获取 Windows 系统原生图标 (支持超大图标)
    static QPixmap getWinIcon(const QString &path);
    
    // 裁剪图标周围的透明区域
    static QPixmap cropTransparent(const QPixmap& pixmap);
    
    // 路径转换工具
    static QString toStoragePath(const QString& path, const QString& fenceId);
    static QString fromStoragePath(const QString& path, const QString& fenceId);
};

#endif // ICONHELPER_H
