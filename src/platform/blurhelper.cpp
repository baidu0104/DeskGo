#include "blurhelper.h"

#ifdef Q_OS_WIN
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// Windows 未公开的 API 结构体定义
enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5,
    ACCENT_INVALID_STATE = 6
};

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    DWORD Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

// WCA_ACCENT_POLICY 属性值
const DWORD WCA_ACCENT_POLICY = 19;

typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

static pfnSetWindowCompositionAttribute pSetWindowCompositionAttribute = nullptr;

static bool initApi()
{
    static bool initialized = false;
    static bool success = false;

    if (!initialized) {
        initialized = true;
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32) {
            pSetWindowCompositionAttribute = 
                (pfnSetWindowCompositionAttribute)GetProcAddress(hUser32, "SetWindowCompositionAttribute");
            success = (pSetWindowCompositionAttribute != nullptr);
        }
    }
    return success;
}

bool BlurHelper::setWindowCompositionAttribute(HWND hwnd, const QColor &tintColor, BlurType type)
{
    if (!initApi() || !pSetWindowCompositionAttribute) {
        return false;
    }

    ACCENT_POLICY accent;
    accent.AccentState = static_cast<ACCENT_STATE>(type);
    accent.AccentFlags = 2;  // 绘制左边框
    // ARGB -> ABGR 格式转换（Windows API 使用 ABGR）
    accent.GradientColor = (tintColor.alpha() << 24) | 
                           (tintColor.blue() << 16) | 
                           (tintColor.green() << 8) | 
                           tintColor.red();
    accent.AnimationId = 0;

    WINDOWCOMPOSITIONATTRIBDATA data;
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &accent;
    data.cbData = sizeof(accent);

    return pSetWindowCompositionAttribute(hwnd, &data);
}

#endif // Q_OS_WIN

bool BlurHelper::enableBlur(QWidget *widget, const QColor &tintColor, BlurType type)
{
    if (!widget) {
        return false;
    }

#ifdef Q_OS_WIN
    // 确保窗口已创建原生句柄
    widget->setAttribute(Qt::WA_TranslucentBackground);
    widget->winId();

    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd) {
        return false;
    }

    // 启用 DWM 扩展框架到客户区
    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    return setWindowCompositionAttribute(hwnd, tintColor, type);
#else
    Q_UNUSED(tintColor)
    Q_UNUSED(type)
    return false;
#endif
}

void BlurHelper::disableBlur(QWidget *widget)
{
    if (!widget) {
        return;
    }

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (hwnd && initApi() && pSetWindowCompositionAttribute) {
        ACCENT_POLICY accent;
        accent.AccentState = ACCENT_DISABLED;
        accent.AccentFlags = 0;
        accent.GradientColor = 0;
        accent.AnimationId = 0;

        WINDOWCOMPOSITIONATTRIBDATA data;
        data.Attrib = WCA_ACCENT_POLICY;
        data.pvData = &accent;
        data.cbData = sizeof(accent);

        pSetWindowCompositionAttribute(hwnd, &data);
    }
#endif
}

bool BlurHelper::isSupported()
{
#ifdef Q_OS_WIN
    return initApi();
#else
    return false;
#endif
}

void BlurHelper::enableRoundedCorners(QWidget *widget)
{
#ifdef Q_OS_WIN
    if (!widget) return;
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (hwnd) {
        // DWMWA_WINDOW_CORNER_PREFERENCE = 33
        // DWMWCP_ROUND = 2
        int preference = 2;
        // 动态加载 DwmSetWindowAttribute 防止在旧系统崩溃（虽然通常 dwmapi.dll 都存在）
        // 这里直接调用，假设构建环境有 dwmapi.lib
        DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));
    }
#else
    Q_UNUSED(widget);
#endif
}
