#include "blurhelper.h"

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <VersionHelpers.h>

// 检测是否为 Windows 11 或更高版本
// Windows 11 的构建号从 22000 开始
static bool isWindows11OrLater()
{
    static int cached = -1;
    if (cached == -1) {
        OSVERSIONINFOEXW osvi;
        memset(&osvi, 0, sizeof(osvi));
        osvi.dwOSVersionInfoSize = sizeof(osvi);
        typedef NTSTATUS (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            RtlGetVersionPtr pRtlGetVersion = (RtlGetVersionPtr)(void*)GetProcAddress(hNtdll, "RtlGetVersion");
            if (pRtlGetVersion) {
                pRtlGetVersion((PRTL_OSVERSIONINFOW)&osvi);
                cached = (osvi.dwBuildNumber >= 22000) ? 1 : 0;
            } else {
                cached = 0;
            }
        } else {
            cached = 0;
        }
    }
    return cached == 1;
}


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
                (pfnSetWindowCompositionAttribute)(void*)GetProcAddress(hUser32, "SetWindowCompositionAttribute");
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

void BlurHelper::enableRoundedCorners(QWidget *widget, int radius)
{
#ifdef Q_OS_WIN
    if (!widget) return;
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd) return;
    
    if (isWindows11OrLater()) {
        // Windows 11: 使用 DWM 原生圆角
        // DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2
        int preference = 2;
        DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));
    } else {
        // Windows 10: 使用窗口区域裁剪实现圆角
        RECT rect;
        GetWindowRect(hwnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        // 注意：CreateRoundRectRgn 的坐标是 (x1, y1, x2, y2)，其中 x2 和 y2 是不包含的边界
        // 所以 x2 应该设为 w，y2 应该设为 h，这样有效区域是 0 到 w-1，0 到 h-1
        // 注意：最后的两个参数是椭圆的宽和高（直径），所以需要将半径 * 2
        HRGN hRgn = CreateRoundRectRgn(0, 0, w, h, radius * 2, radius * 2);
        SetWindowRgn(hwnd, hRgn, TRUE);
        // 注意：SetWindowRgn 会接管 hRgn 的所有权，不需要手动删除
    }
#else
    Q_UNUSED(widget);
    Q_UNUSED(radius);
#endif
}

bool BlurHelper::isWindows11()
{
#ifdef Q_OS_WIN
    return isWindows11OrLater();
#else
    return false;
#endif
}
