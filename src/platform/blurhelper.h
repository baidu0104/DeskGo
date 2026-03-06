#ifndef BLURHELPER_H
#define BLURHELPER_H

#include <QWidget>
#include <QColor>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * @brief 毛玻璃效果辅助类
 * 封装 Windows DWM API 实现 Acrylic Blur 效果
 */
class BlurHelper
{
public:
    enum BlurType {
        None = 0,
        Blur = 3,           // Windows 10 标准模糊
        Acrylic = 4,        // Windows 11 亚克力效果
        HostBackdrop = 5    // 系统背景效果
    };

    /**
     * @brief 为窗口启用毛玻璃效果
     * @param widget 目标窗口
     * @param tintColor 着色颜色（ARGB格式，Alpha控制透明度）
     * @param type 模糊类型
     * @return 是否成功
     */
    static bool enableBlur(QWidget *widget, const QColor &tintColor = QColor(30, 30, 30, 200), BlurType type = Acrylic);

    /**
     * @brief 禁用毛玻璃效果
     * @param widget 目标窗口
     */
    static void disableBlur(QWidget *widget);

    /**
     * @brief 检查系统是否支持毛玻璃效果
     * @return 是否支持
     */
    static bool isSupported();
    
    /**
     * @brief 启用圆角效果
     * @param widget 目标窗口
     * @param radius 圆角半径 (仅在 Windows 10 上使用)
     * Windows 11 使用 DWM 原生圆角，Windows 10 使用 SetWindowRgn 裁剪
     */
    static void enableRoundedCorners(QWidget *widget, int radius = 10);
    
    /**
     * @brief 检查是否为 Windows 11 或更高版本
     * @return 是否为 Windows 11+
     */
    static bool isWindows11();

private:
#ifdef Q_OS_WIN
    static bool setWindowCompositionAttribute(HWND hwnd, const QColor &tintColor, BlurType type);
#endif
};

#endif // BLURHELPER_H
