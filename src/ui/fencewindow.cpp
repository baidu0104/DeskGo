#include "fencewindow.h"
#include "iconwidget.h"
#include "flowlayout.h"
#include "../platform/blurhelper.h"
#include "../core/configmanager.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QMenu>
#include <QJsonObject>
#include <QJsonArray>
#include <QScreen>
#include <QApplication>
#include <QMimeData>
#include <QVariantAnimation>

#include <QFileInfo>
#include <QFileIconProvider>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>
#include <QDebug>
#include <QTimer>
#include "../platform/desktophelper.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>

// WM_MOUSEACTIVATE 返回值
#ifndef MA_NOACTIVATE
#define MA_NOACTIVATE 3
#endif
#endif

#include <QtWin>
#include <shlobj.h>
#include <commoncontrols.h>

// 确保定义 SHIL_JUMBO，以防编译环境缺失
#ifndef SHIL_JUMBO
#define SHIL_JUMBO 0x4
#endif
#ifndef SHIL_EXTRALARGE
#define SHIL_EXTRALARGE 0x2
#endif

// 定义 IImageList 接口 ID
// UUID: 46EB5926-582E-4017-9FDF-E8998DAA0950
static const GUID IID_IImageList_LOCAL = { 0x46EB5926, 0x582E, 0x4017, { 0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50 } };

// 日志记录函数
void logToDesktop(const QString &msg) {
    Q_UNUSED(msg);
    // 调试日志已禁用
}

// 静态成员初始化
HHOOK FenceWindow::s_hKeyboardHook = NULL;
QSet<FenceWindow*> FenceWindow::s_allFences;
HHOOK FenceWindow::s_hMouseHook = NULL;
QPointer<FenceWindow> FenceWindow::s_editingFence;

// 键盘钩子回调函数
LRESULT CALLBACK FenceWindow::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *pKbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        
        // 检测 Win+D 组合键
        // D 键的虚拟键码是 0x44
        if (pKbdStruct->vkCode == 0x44) { // D 键
            // 检查 Win 键是否按下
            if (GetAsyncKeyState(VK_LWIN) & 0x8000 || GetAsyncKeyState(VK_RWIN) & 0x8000) {
                if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                    qDebug() << "[KeyboardProc] Win+D detected, manual handling...";
                    
                    static bool s_inShowDesktopMode = false;
                    static QList<HWND> s_windowsToRestore;
                    
                    if (!s_inShowDesktopMode) {
                        // 进入显示桌面模式：最小化除了围栏外的所有窗口
                        s_windowsToRestore.clear();
                        
                        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                            // 跳过自己管理的围栏窗口
                            for (FenceWindow* fence : s_allFences) {
                                 if ((HWND)fence->winId() == hwnd) return TRUE;
                            }
                            
                            // 跳过 Shell 核心窗口
                            HWND hShellWnd = GetShellWindow();
                            if (hwnd == hShellWnd) return TRUE;

                            WCHAR className[256];
                            GetClassName(hwnd, className, 256);
                            if (wcscmp(className, L"Progman") == 0 || 
                                wcscmp(className, L"WorkerW") == 0 || 
                                wcscmp(className, L"Shell_TrayWnd") == 0 || 
                                wcscmp(className, L"Shell_SecondaryTrayWnd") == 0) {
                                return TRUE;
                            }
                            
                            if (!IsIconic(hwnd) && IsWindowVisible(hwnd)) {
                                 // 也是排除工具窗口
                                 LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
                                 if ((exStyle & WS_EX_TOOLWINDOW) == 0) {
                                     // 仅记录并最小化非工具窗口、可见的窗口
                                     PostMessage(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                                     
                                     // 记录到列表中以便恢复
                                     QList<HWND> *pList = reinterpret_cast<QList<HWND>*>(lParam);
                                     if (pList) pList->append(hwnd);
                                 }
                            }
                            return TRUE;
                        }, (LPARAM)&s_windowsToRestore);

                        s_inShowDesktopMode = true;
                    } else {
                        // 退出显示桌面模式：仅恢复我们之前最小化的窗口
                        // 这解决了"恢复了莫名其妙的窗口"的问题
                        
                        for (HWND hwnd : s_windowsToRestore) {
                            if (IsWindow(hwnd)) {
                                // 仅恢复当前仍然是最小化状态的窗口
                                // 如果用户在 ShowDesktop 模式下手动恢复了某个窗口，我们就不去动它
                                if (IsIconic(hwnd)) {
                                     PostMessage(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
                                }
                            }
                        }
                        
                        s_windowsToRestore.clear();
                        s_inShowDesktopMode = false;
                    }
                    
                    // 拦截事件，不让系统处理
                    return 1;
                }
            }
        }
    }
    
    // 继续传递其他按键事件
    return CallNextHookEx(s_hKeyboardHook, nCode, wParam, lParam);
}

// 鼠标钩子回调函数
LRESULT CALLBACK FenceWindow::MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        // 使用 QPointer 检查对象是否仍然有效
        if (!s_editingFence.isNull()) {
            // 检测鼠标左键按下
            if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
                MSLLHOOKSTRUCT *pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
                
                // 获取点击的全局位置
                POINT pt = pMouseStruct->pt;
                
                // 使用 Windows API 获取窗口句柄和矩形
                HWND hWnd = (HWND)s_editingFence->winId();
                
                // 验证窗口句柄是否有效
                if (hWnd && IsWindow(hWnd)) {
                    RECT rect;
                    if (GetWindowRect(hWnd, &rect)) {
                        // 检查点击是否在围栏窗口外部
                        if (pt.x < rect.left || pt.x > rect.right || 
                            pt.y < rect.top || pt.y > rect.bottom) {
                            // 点击在围栏外部，通过信号通知主线程完成编辑
                            QMetaObject::invokeMethod(s_editingFence.data(), "finishTitleEdit", Qt::QueuedConnection);
                        }
                    }
                }
            }
        }
    }
    
    // 继续传递鼠标事件
    return CallNextHookEx(s_hMouseHook, nCode, wParam, lParam);
}

// 辅助函数：裁剪透明边框
// 用于处理那些画布很大但实际内容很小的图标 (如某些 256x256 图标实际只有 32x32 的内容)
static QPixmap cropTransparent(const QPixmap& pixmap) {
    if (pixmap.isNull()) return pixmap;
    
    QImage img = pixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int w = img.width();
    int h = img.height();
    
    // 如果图片已经很小，就不需要处理了
    if (w <= 48 && h <= 48) return pixmap;

    int top = -1, bottom = -1, left = -1, right = -1;

    // 扫描上边界
    for (int y = 0; y < h; ++y) {
        const QRgb *scanLine = (const QRgb *)img.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (qAlpha(scanLine[x]) > 10) { // 阈值 > 10 忽略极淡的杂色
                top = y;
                break;
            }
        }
        if (top != -1) break;
    }

    if (top == -1) return pixmap; // 全透明

    // 扫描下边界
    for (int y = h - 1; y >= top; --y) {
        const QRgb *scanLine = (const QRgb *)img.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (qAlpha(scanLine[x]) > 10) {
                bottom = y;
                break;
            }
        }
        if (bottom != -1) break;
    }

    // 扫描左边界
    for (int x = 0; x < w; ++x) {
        for (int y = top; y <= bottom; ++y) {
             if (qAlpha(img.pixel(x, y)) > 10) {
                 left = x;
                 break;
             }
        }
        if (left != -1) break;
    }

    // 扫描右边界
    for (int x = w - 1; x >= left; --x) {
        for (int y = top; y <= bottom; ++y) {
             if (qAlpha(img.pixel(x, y)) > 10) {
                 right = x;
                 break;
             }
        }
        if (right != -1) break;
    }
    
    QRect validRect(left, top, right - left + 1, bottom - top + 1);
    
    // 只有当有效区域显著小于原图时才裁剪 (例如有效面积 < 原图的 40% 或者 边长 < 60%)
    // 对于 256x256 的图，如果内容只有 128x128 (50% 边长)，裁剪是有意义的
    if (validRect.width() < w * 0.7 && validRect.height() < h * 0.7) {
        // 稍微留一点边距 (10%)
        int padding = qMax(validRect.width(), validRect.height()) * 0.1;
        validRect.adjust(-padding, -padding, padding, padding);
        
        // 确保不越界
        validRect = validRect.intersected(img.rect());
        
        // 只有当裁剪后的尺寸仍然有意义时才返回
        if (validRect.isValid() && validRect.width() > 0 && validRect.height() > 0) {
             return QPixmap::fromImage(img.copy(validRect));
        }
    }

    return pixmap;
}

// 获取系统原生图标辅助函数
static QPixmap getWinIcon(const QString &path) {
    // 确保 COM 初始化
    CoInitialize(NULL);
    
    QString nativePath = QDir::toNativeSeparators(path);
    QPixmap result;
    
    SHFILEINFO shfi;
    memset(&shfi, 0, sizeof(shfi));

    // 尝试1: 使用 SHGetImageList 获取超大图标 (Jumbo/ExtraLarge)
    // 首先获取系统图标索引
    if (SHGetFileInfo((const wchar_t*)nativePath.utf16(), 0, &shfi, sizeof(shfi), 
                      SHGFI_SYSICONINDEX)) {
        
        IImageList *imageList = nullptr;
        HRESULT hr = E_FAIL;
        
        // 优先尝试获取 Jumbo 图标 (256x256)
        hr = SHGetImageList(SHIL_JUMBO, IID_IImageList_LOCAL, (void**)&imageList);
        
        // 如果失败，尝试获取 ExtraLarge 图标 (48x48)
        if (FAILED(hr)) {
            hr = SHGetImageList(SHIL_EXTRALARGE, IID_IImageList_LOCAL, (void**)&imageList);
        }

        if (SUCCEEDED(hr) && imageList) {
            HICON hIcon = nullptr;
            // 获取图标
            hr = imageList->GetIcon(shfi.iIcon, ILD_TRANSPARENT, &hIcon);
            
            if (SUCCEEDED(hr) && hIcon) {
                QPixmap pixmap = QtWin::fromHICON(hIcon);
                DestroyIcon(hIcon);
                
                if (!pixmap.isNull()) {
                    // 裁剪可能的透明边框
                    result = cropTransparent(pixmap);
                }
            }
            imageList->Release();
        }
    }

    // 尝试2: 如果上面失败了，回退到标准的 SHGetFileInfo (通常是 32x32)
    if (result.isNull()) {
        memset(&shfi, 0, sizeof(shfi));
        if (SHGetFileInfo((const wchar_t*)nativePath.utf16(), 0, &shfi, sizeof(shfi), 
                          SHGFI_ICON | SHGFI_LARGEICON)) {
            QPixmap pixmap = QtWin::fromHICON(shfi.hIcon);
            DestroyIcon(shfi.hIcon);
            if (!pixmap.isNull()) {
                result = pixmap; // 返回 32x32 图标
            }
        }
    }
    
    // 尝试3: 通过 ExtractIconEx (针对 exe/dll)
    if (result.isNull() && (path.endsWith(".exe", Qt::CaseInsensitive) || path.endsWith(".dll", Qt::CaseInsensitive))) {
        HICON hIconLarge = NULL;
        UINT extracted = ExtractIconEx((const wchar_t*)nativePath.utf16(), 0, &hIconLarge, NULL, 1);
        if (extracted > 0 && hIconLarge) {
            QPixmap pixmap = QtWin::fromHICON(hIconLarge);
            DestroyIcon(hIconLarge);
            if (!pixmap.isNull()) {
                 result = pixmap;
            }
        }
    }

    CoUninitialize();
    
    return result;
}

FenceWindow::FenceWindow(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_title(title)
{
    // 将当前窗口添加到全局集合
    s_allFences.insert(this);
    
    // 如果是第一个窗口，安装键盘钩子
    if (s_allFences.size() == 1 && s_hKeyboardHook == NULL) {
#ifdef Q_OS_WIN
        s_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
        if (s_hKeyboardHook) {
            qDebug() << "[FenceWindow] Keyboard hook installed successfully";
        } else {
            qDebug() << "[FenceWindow] Failed to install keyboard hook, error:" << GetLastError();
        }
#endif
    }
    
    // 初始化保存定时器 (防抖动) - 必须在 setupUi 之前，因为 setupUi 会触发 resizeEvent
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(1000); // 1秒后保存
    connect(m_saveTimer, &QTimer::timeout, this, &FenceWindow::geometryChanged);
    
    // 在 setupUi 之前先隐藏窗口，防止在设置过程中显示
    setVisible(false);
    
    setupUi();
}

FenceWindow::~FenceWindow()
{
    m_isClosing = true;
    
    // 如果当前窗口正在编辑，清理鼠标钩子
    if (s_editingFence.data() == this) {
#ifdef Q_OS_WIN
        if (s_hMouseHook) {
            UnhookWindowsHookEx(s_hMouseHook);
            s_hMouseHook = NULL;
            qDebug() << "[~FenceWindow] Mouse hook uninstalled in destructor";
        }
#endif
        s_editingFence.clear();
    }
    
    // 从全局集合中移除
    s_allFences.remove(this);
    
    // 如果是最后一个窗口，卸载键盘钩子
    if (s_allFences.isEmpty() && s_hKeyboardHook != NULL) {
#ifdef Q_OS_WIN
        UnhookWindowsHookEx(s_hKeyboardHook);
        s_hKeyboardHook = NULL;
        qDebug() << "[~FenceWindow] Keyboard hook uninstalled";
#endif
    }
}

void FenceWindow::setupUi()
{
    // 设置窗口属性
#ifdef Q_OS_WIN
    // 使用 Qt::Window 而不是 Qt::Tool，以便接收系统消息（如 WM_SYSCOMMAND）
    // 配合 WS_EX_TOOLWINDOW 扩展样式可以不在任务栏显示
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
#else
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
#endif
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating); // 显示时不抢焦点，防止遮挡当前活动窗口
    
    setMinimumSize(180, 60);
    resize(280, 200);
    setMouseTracking(true);
    setAcceptDrops(true);  // 接受拖放

#ifdef Q_OS_WIN
    // 圆角效果将在 showEvent 中通过 BlurHelper 统一处理
#endif

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 标题栏
    m_titleLabel = new QLabel(m_title, this);
    m_titleLabel->setFixedHeight(32);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(R"(
        QLabel {
            color: #ffffff;
            font-size: 12px;
            font-weight: 500;
            background: transparent;
            padding: 0 10px;
        }
    )");
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    mainLayout->addWidget(m_titleLabel);

    // 内容区域
    m_contentArea = new QWidget(this);
    m_contentArea->setObjectName("contentArea");
    m_contentLayout = new FlowLayout(m_contentArea, 8, 6, 6);  // 流式布局：边距8，水平间距6，垂直间距6
    mainLayout->addWidget(m_contentArea, 1);

    // 折叠动画将在 setCollapsed 中按需创建
    
    // 启用鼠标追踪并安装事件过滤器，以便在子控件上悬浮时也能更新光标
    m_contentArea->setMouseTracking(true);
    m_contentArea->installEventFilter(this);
}

void FenceWindow::setupBlurEffect()
{
    // Windows 11: 使用原生圆角
    if (BlurHelper::isWindows11()) {
         BlurHelper::enableRoundedCorners(this, 10);
    }
    // 不启用 DWM 模糊，使用纯 Qt 半透明背景
    // 避免兼容性问题和拖动闪烁
}

void FenceWindow::showEvent(QShowEvent *event)
{
    // 保存当前几何位置，防止 setWindowToDesktop 或其他操作重置它
    QRect geo = geometry();
    
    QWidget::showEvent(event);
    
    // 只在第一次显示时调用 setWindowToDesktop
    if (!m_desktopEmbedded) {
        // 移除延迟，直接同步执行
        // 任何延迟都会导致肉眼可见的"下沉"过程
        
        // 1. 先发制人，在逻辑处理前直接物理置底
        HWND hWnd = (HWND)winId();
        SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        
        // 2. 执行桌面嵌入逻辑（初始化 Watchdog 等）
        DesktopHelper::setWindowToDesktop(this);
        m_desktopEmbedded = true;
        
        // 3. (已移除) 守护定时器不再需要，由 WM_WINDOWPOSCHANGING 拦截处理
        
        // 4. 发出信号
        emit firstShowCompleted();
    } else {
        // 从隐藏恢复显示时，重新设置窗口位置到底层
        HWND hWnd = (HWND)winId();
        SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    
    setupBlurEffect();
    
    // 强制恢复尺寸和位置，使用 QTimer 确保在窗口系统事件处理完后执行

}

void FenceWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    
    // Windows 10 不需要手动更新区域，完全依赖 Qt 的 TranslucentBackground
    if (BlurHelper::isWindows11()) {
        // Win11 原生圆角不需要在 resize 时重新设置，但为了保险起见保持不变或移除
    }
    
    // 更新 Expanded Height (仅在展开状态下更新)
    if (!m_collapsed) {
        m_expandedHeight = height();
    }

    // 触发保存（使用防抖动延迟）
    if (m_saveTimer) {
        m_saveTimer->start();
    }
}

QString FenceWindow::title() const
{
    return m_title;
}

void FenceWindow::setTitle(const QString &title)
{
    if (m_title != title) {
        m_title = title;
        m_titleLabel->setText(title);
        emit titleChanged(title);
    }
}

bool FenceWindow::isCollapsed() const
{
    return m_collapsed;
}

// 辅助：移除旧的动画连接（如果存在）
void FenceWindow::setCollapsed(bool collapsed)
{
    if (m_collapsed != collapsed) {
        // 如果正在编辑标题，先完成编辑
        if (m_titleEdit) {
            finishTitleEdit();
        }

        m_collapsed = collapsed;
        
        // 停止现有动画
        if (m_collapseAnimation) {
            m_collapseAnimation->stop();
        }

        int startHeight = height();
        int endHeight = 0;

        // 统一在动画开始前解除所有尺寸限制
        setMinimumHeight(0);
        setMaximumHeight(16777215);

        if (collapsed) {
            // 折叠
            m_expandedHeight = height();
            if (m_expandedHeight < 64) m_expandedHeight = 200; 
            
            endHeight = 32;
            m_contentArea->setVisible(false);
        } else {
            // 展开
            endHeight = m_expandedHeight;
            if (endHeight < 64) endHeight = 200;
            
            m_contentArea->setVisible(true);
        }

        // 创建新的变量动画直接控制高度
        if (!m_collapseAnimation) {
             m_collapseAnimation = new QVariantAnimation(this);
        }
        
        m_collapseAnimation->setDuration(200);
        m_collapseAnimation->setStartValue(startHeight);
        m_collapseAnimation->setEndValue(endHeight);
        m_collapseAnimation->setEasingCurve(QEasingCurve::OutCubic);

        // 必须先断开旧连接，否则会重复调用
        m_collapseAnimation->disconnect(this);

        connect(m_collapseAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            // 使用 Windows API 直接设置高度，绕过 Qt 的几何约束
#ifdef Q_OS_WIN
            HWND hWnd = (HWND)winId();
            SetWindowPos(hWnd, NULL, 0, 0, width(), value.toInt(), 
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
#else
            resize(width(), value.toInt());
#endif
        });

        connect(m_collapseAnimation, &QVariantAnimation::finished, this, [this, collapsed, endHeight]() {
            // 动画结束，应用最终状态的限制
            if (collapsed) {
                setMinimumHeight(32);
                setMaximumHeight(32); // 锁定在折叠高度
            } else {
                setMinimumHeight(64); // 恢复最小高度限制
                setMaximumHeight(16777215); // 解除最大高度限制
                resize(width(), endHeight); // 确保最终高度正确
            }
            emit collapsedChanged(collapsed);
            emit geometryChanged();
        });

        m_collapseAnimation->start(); // 不要使用 DeleteWhenStopped，因为我们复用了成员变量
    }
}

void FenceWindow::addIcon(IconWidget *icon)
{
    if (!icon) return;

    qDebug() << "[addIcon] Adding icon:" << icon->path();
    qDebug() << "  m_collapsed:" << m_collapsed;
    qDebug() << "  m_contentArea visible:" << m_contentArea->isVisible();
    qDebug() << "  m_contentArea size:" << m_contentArea->size();

    // 检查是否已存在相同路径的图标
    for (IconWidget *existingIcon : m_icons) {
        if (existingIcon->path() == icon->path()) {
            qDebug() << "  Icon already exists, deleting duplicate";
            icon->deleteLater(); // 删除重复的图标对象
            return;
        }
    }

    // 连接删除信号
    connect(icon, &IconWidget::removeRequested, [this, icon]() {
        removeIcon(icon);
    });

    m_icons.append(icon);
    
    // 应用当前的文字显示设置
    icon->setTextVisible(ConfigManager::instance()->iconTextVisible());
    
    icon->setParent(m_contentArea);
    m_contentLayout->addWidget(icon);
    icon->show();
    
    qDebug() << "  icon visible:" << icon->isVisible();
    qDebug() << "  icon size:" << icon->size();
    qDebug() << "  icon pos:" << icon->pos();
    qDebug() << "  layout count:" << m_contentLayout->count();
    
    // 强制更新布局
    m_contentLayout->update();
    m_contentArea->update();
    update();
    
    emit geometryChanged(); // 保存更改
}

void FenceWindow::removeIcon(IconWidget *icon)
{
    if (icon && m_icons.contains(icon)) {
        // 如果是来自桌面的图标，尝试恢复回去
        if (icon->data().isFromDesktop) {
            QString srcPath = icon->path();
            QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
            QFileInfo fileInfo(srcPath);
            QString targetPath = desktopPath + "/" + fileInfo.fileName();
            
            // 移动文件回去
            if (QFile::exists(srcPath)) {
                // 处理重名（虽然理论上如果一直在运行不会重名，但可能用户手动放了个同名文件）
                if (QFile::exists(targetPath)) {
                    // 简单的重命名策略
                    targetPath = desktopPath + "/restored_" + fileInfo.fileName();
                }
                
                if (QFile::rename(srcPath, targetPath)) {
                    // 使用精确的文件通知，刷新更快
                    DesktopHelper::notifyFileAdded(targetPath);
                    
                    // 延迟设置位置，等待系统识别新文件
                    QPoint originalPos = icon->data().originalPosition;
                    
                    // 800ms 后尝试设置位置（有内置重试机制）
                    QTimer::singleShot(800, [targetPath, originalPos]() {
                        DesktopHelper::setIconPosition(targetPath, originalPos);
                    });
                }
            }
        }

        m_icons.removeOne(icon);
        m_contentLayout->removeWidget(icon);
        icon->deleteLater(); // 销毁对象
        emit geometryChanged(); // 触发保存
    }
}


void FenceWindow::setIconTextVisible(bool visible)
{
    for (IconWidget *icon : m_icons) {
        icon->setTextVisible(visible);
    }
}

void FenceWindow::restoreAllIcons()
{
    // 创建一个临时副本，因为 removeIcon 会修改 list
    auto iconsCopy = m_icons;
    for (IconWidget *icon : iconsCopy) {
        removeIcon(icon);
    }
}

QList<IconWidget*> FenceWindow::icons() const
{
    return m_icons;
}

QJsonObject FenceWindow::toJson() const
{
    QJsonObject obj;
    obj["id"] = m_id;
    obj["title"] = m_title;
    obj["x"] = x();
    obj["y"] = y();
    obj["width"] = width();
    obj["height"] = height();
    obj["collapsed"] = m_collapsed;
    obj["expandedHeight"] = m_expandedHeight;
    // 不再保存 alwaysOnTop，因为我们已经通过键盘钩子解决了 Win+D 问题
    // obj["alwaysOnTop"] = m_alwaysOnTop;
    
    QJsonArray iconsArray;
    for (IconWidget *icon : m_icons) {
        QJsonObject iconObj;
        iconObj["name"] = icon->name();
        iconObj["path"] = icon->path();
        
        IconWidget::IconData data = icon->data();
        if (data.isFromDesktop) {
            iconObj["isFromDesktop"] = true;
            iconObj["originalX"] = data.originalPosition.x();
            iconObj["originalY"] = data.originalPosition.y();
        }
        
        iconsArray.append(iconObj);
    }
    obj["icons"] = iconsArray;

    return obj;
}

FenceWindow* FenceWindow::fromJson(const QJsonObject &json)
{
    FenceWindow *fence = new FenceWindow(json["title"].toString("新围栏"));
    
    QString id = json["id"].toString();
    if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    fence->setId(id);
    
    int x = json.contains("x") ? json["x"].toInt() : 100;
    int y = json.contains("y") ? json["y"].toInt() : 100;
    int w = json.contains("width") ? json["width"].toInt() : 280;
    int h = json.contains("height") ? json["height"].toInt() : 200;
    
    // 确保在屏幕范围内或至少有最小尺寸
    if (w < 100) w = 280;
    if (h < 50) h = 200;
    
    // 先设置正确的几何位置
    fence->setGeometry(x, y, w, h);
    
    // 关键：创建窗口句柄但不显示
    // 这样可以让 setWindowToDesktop 工作，但窗口不会显示
    fence->winId(); // 强制创建窗口句柄
    
    if (json["collapsed"].toBool()) {
        // 如果保存时是折叠的，那么 json["height"] 是 32。
        // 我们不能用它作为展开高度，否则展开后还是 32。
        // 尝试读取 expandedHeight，如果没有则给默认值 300
        if (json.contains("expandedHeight")) {
            fence->m_expandedHeight = json["expandedHeight"].toInt();
        } else {
            fence->m_expandedHeight = 300; 
        }
        
        fence->m_collapsed = true;
        // 关键：复刻 setCollapsed(true) 的逻辑
        fence->m_contentArea->setVisible(false);
        fence->setMinimumHeight(32);
        fence->setMaximumHeight(32);
        fence->resize(json["width"].toInt(), 32);
    } else {
        // 确保非折叠状态下内容区域可见
        fence->setMinimumHeight(64);
        fence->m_contentArea->setVisible(true);
    }
    
    // 不再恢复 alwaysOnTop 设置，因为我们已经通过键盘钩子解决了 Win+D 问题
    // if (json.contains("alwaysOnTop") && json["alwaysOnTop"].toBool()) {
    //     fence->m_alwaysOnTop = true;
    // }

    // 恢复图标
    QJsonArray iconsArray = json["icons"].toArray();
    QFileIconProvider iconProvider;

    for (const QJsonValue &val : iconsArray) {
        QJsonObject iconObj = val.toObject();
        QString path = iconObj["path"].toString();
        QString name = iconObj["name"].toString();
        
        QFileInfo fileInfo(path);
        if (fileInfo.exists()) {
             IconWidget::IconData data;
             data.name = name;
             data.path = path;
             data.targetPath = path;
             // 优先使用 WinAPI 获取图标
             data.icon = getWinIcon(path);
             
             if (data.icon.isNull()) {
                 data.icon = iconProvider.icon(fileInfo).pixmap(48, 48);
             }
             
             if (iconObj.contains("isFromDesktop") && iconObj["isFromDesktop"].toBool()) {
                 data.isFromDesktop = true;
                 data.originalPosition = QPoint(iconObj["originalX"].toInt(), iconObj["originalY"].toInt());
             }

             IconWidget *icon = new IconWidget(data);
             fence->addIcon(icon);
        }
    }

    return fence;
}

bool FenceWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
#ifdef Q_OS_WIN
    MSG *msg = static_cast<MSG*>(message);
    
    // 处理 WM_MOUSEACTIVATE 消息，防止点击时激活窗口
    if (msg->message == WM_MOUSEACTIVATE) {
        // 始终返回 MA_NOACTIVATE，不激活窗口但保留鼠标消息
        // 即使在编辑标题时也不激活窗口，避免 Z-order 改变
        *result = MA_NOACTIVATE;
        return true;
    }

    // 拦截 WM_PARENTNOTIFY，防止子窗口（如菜单）创建/销毁时激活父窗口
    if (msg->message == WM_PARENTNOTIFY) {
        if (LOWORD(msg->wParam) == WM_CREATE || LOWORD(msg->wParam) == WM_DESTROY) {
             // 不做任何处理，直接返回，避免默认处理可能带来的激活行为
        }
    }

    // 拦截 WM_SYSCOMMAND，防止通过系统菜单或快捷键最小化
    if (msg->message == WM_SYSCOMMAND) {
        if ((msg->wParam & 0xFFF0) == SC_MINIMIZE) {
            *result = 0;
            return true;
        }
    }

    // 拦截 WM_NCACTIVATE，防止非客户区（标题栏）显示为激活颜色
    // 同时也阻止窗口状态变为 "Active"
    if (msg->message == WM_NCACTIVATE) {
        // 强制返回 TRUE，表示我们处理了该消息，并且保持非激活外观（wParam=FALSE 当我们不像让它激活时）
        // 这里不管 wParam 是什么，都假装它是 FALSE（非激活），但实际上我们直接返回 true 跳过默认处理
        // 如果想让它看起来总是非激活，可以忽略 wParam，直接调用 DefWindowProc(..., FALSE, ...)
        // 但 Qt 可能会混淆。最简单的：
        if (msg->wParam) {
            *result = TRUE; // 告诉系统 "即使你想激活，我也画成非激活的样子"
            return true; 
        }
    }
    
    // 键盘钩子已经在全局拦截 Win+D，这里不需要额外处理
    
    // 只记录 WM_SHOWWINDOW 消息（用于调试 Win+D）
    // if (msg->message == WM_SHOWWINDOW) {
    //     qDebug() << "[nativeEvent] WM_SHOWWINDOW wParam:" << msg->wParam << "lParam:" << msg->lParam;
    // }
    
    // 拦截 WM_WINDOWPOSCHANGING 以防止闪烁和隐藏
    if (msg->message == WM_WINDOWPOSCHANGING) {
        // 只有在嵌入桌面模式且非置顶状态下才干预
        // 但如果是用户主动隐藏（m_userHidden），则不拦截
        if (m_desktopEmbedded && !m_alwaysOnTop && !m_userHidden) {
            WINDOWPOS* pos = (WINDOWPOS*)msg->lParam;
            
            // 1. 防止隐藏 (Win+D 会尝试隐藏窗口)
            if (pos->flags & SWP_HIDEWINDOW) {
                pos->flags &= ~SWP_HIDEWINDOW;
                pos->flags |= SWP_SHOWWINDOW;
            }

            // 2. 防止 Z 序上浮
            // 如果试图改变 Z 序 (没有 SWP_NOZORDER)
            if (!(pos->flags & SWP_NOZORDER)) {
                // 如果试图将窗口放置在非底部的位置
                if (pos->hwndInsertAfter != HWND_BOTTOM) {
                    pos->hwndInsertAfter = HWND_BOTTOM;
                }
            }
        }
    }
    

    
    if (msg->message == WM_ERASEBKGND) {
        *result = 1;
        return true;
    }

    if (msg->message == WM_NCHITTEST) {
        // 如果窗口被折叠，只允许通过标题栏拖拽，不允许调整大小
        // 如果窗口被折叠，允许调整左右大小
        if (m_collapsed) {
             QPoint globalPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
             QPoint lp = mapFromGlobal(globalPos);
             
             int x = lp.x();
             int w = width();
             const int border = 8;
             
             if (x < border) {
                 *result = HTLEFT;
                 return true;
             }
             if (x >= w - border) {
                 *result = HTRIGHT;
                 return true;
             }
             
             // 其他区域返回 HTCLIENT，让 Qt 处理拖动
             // 不使用 HTCAPTION 以避免与 Qt 的拖动逻辑冲突
             *result = HTCLIENT;
             return true;
        }

        QPoint globalPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
        QPoint lp = mapFromGlobal(globalPos);
        
        int x = lp.x();
        int y = lp.y();
        int w = width();
        int h = height();
        
        const int border = 8; // 边缘响应范围
        
        // 调整大小检测
        bool left = x < border;
        bool right = x >= w - border;
        bool top = y < border;
        bool bottom = y >= h - border;
        
        if (top && left) {
            *result = HTTOPLEFT;
            return true;
        }
        if (top && right) {
            *result = HTTOPRIGHT;
            return true;
        }
        if (bottom && left) {
            *result = HTBOTTOMLEFT;
            return true;
        }
        if (bottom && right) {
            *result = HTBOTTOMRIGHT;
            return true;
        }
        if (left) {
            *result = HTLEFT;
            return true;
        }
        if (right) {
            *result = HTRIGHT;
            return true;
        }
        if (top) {
            *result = HTTOP;
            return true;
        }
        if (bottom) {
            *result = HTBOTTOM;
            return true;
        }
        
        // 标题栏检测 (用于移动)
        // 返回 HTCLIENT 让 Qt 处理拖动，不使用 HTCAPTION 以避免与 Qt 的拖动逻辑冲突
        if (titleBarRect().contains(lp)) {
            *result = HTCLIENT;
            return true;
        }
        
        // 其他区域明确返回 HTCLIENT，确保子控件接收鼠标事件
        *result = HTCLIENT;
        return true;
    }
    // 另外，当调整大小时，我们需要通知 Qt 更新布局，
    // 因为通过 WM_NCHITTEST 调整大小绕过了 Qt 的 resizeEvent 吗？不，Qt 会收到 WM_SIZE。
    // 但是我们需要在大小改变后保存位置。
    if (msg->message == WM_EXITSIZEMOVE) {
        emit geometryChanged();
    }
    
    // 处理标题栏双击：启动编辑模式而不是最大化
    if (msg->message == WM_NCLBUTTONDBLCLK) {
        if (msg->wParam == HTCAPTION) {
            startTitleEdit();
            *result = 0;
            return true;  // 我们处理了，不让 Windows 处理
        }
    }
    
    // 处理客户区双击：如果在标题栏区域，启动编辑模式
    // 这是因为 WM_NCHITTEST 返回 HTCLIENT 而不是 HTCAPTION，
    // 所以双击事件会作为 WM_LBUTTONDBLCLK 而不是 WM_NCLBUTTONDBLCLK 发送
    if (msg->message == WM_LBUTTONDBLCLK) {
        QPoint localPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
        if (titleBarRect().contains(localPos)) {
            startTitleEdit();
            *result = 0;
            return true;
        }
    }
    
    // 处理标题栏右键：显示自定义菜单而不是系统菜单
    if (msg->message == WM_NCRBUTTONUP) {
        if (msg->wParam == HTCAPTION) {
            // 手动触发上下文菜单事件
            // 注意：WM_NCRBUTTONUP 的坐标是屏幕坐标
            // 需要包含 <QContextMenuEvent>
            QPoint globalPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
            QContextMenuEvent e(QContextMenuEvent::Mouse, mapFromGlobal(globalPos), globalPos);
            contextMenuEvent(&e);
            
            *result = 0;
            return true; // 我们处理了
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void FenceWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    // 绘制圆角背景和边框
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    
    QPainterPath path;
    path.addRoundedRect(rect(), 10, 10);
    
    // 背景色 - 半透明深色背景
    p.fillPath(path, QColor(30, 30, 35, 200));
    
    // 边框
    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.drawPath(path);

    // 标题栏分隔线
    if (!m_collapsed) {
        p.setPen(QPen(QColor(255, 255, 255, 20), 1));
        p.drawLine(10, 32, width() - 10, 32);
    }
    
    // 绘制拖拽插入位置指示器
    if (m_showDropIndicator && !m_dropIndicatorRect.isNull()) {
        // 将内容区域的坐标转换为窗口坐标
        QRect indicatorRect = m_dropIndicatorRect.translated(m_contentArea->pos());
        
        // 绘制蓝色半透明竖线
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(100, 150, 255, 180));
        p.drawRoundedRect(indicatorRect, 2, 2);
        
        // 绘制边缘高光
        p.setPen(QPen(QColor(150, 200, 255, 220), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(indicatorRect, 2, 2);
    }
}

QRect FenceWindow::titleBarRect() const
{
    return QRect(0, 0, width(), 32);
}

// 边缘吸附：拖动时计算吸附后的位置
QPoint FenceWindow::snapPositionToOtherFences(const QPoint& targetPos, const QSize& targetSize) const
{
    QPoint snappedPos = targetPos;
    int snapX = SNAP_THRESHOLD + 1; // 当前最小X吸附距离
    int snapY = SNAP_THRESHOLD + 1; // 当前最小Y吸附距离
    
    // 当前围栏的目标边缘
    int myLeft = targetPos.x();
    int myRight = targetPos.x() + targetSize.width();
    int myTop = targetPos.y();
    int myBottom = targetPos.y() + targetSize.height();
    
    // 遍历所有其他围栏
    for (FenceWindow* other : s_allFences) {
        if (other == this) continue;
        
        QRect otherRect = other->geometry();
        int otherLeft = otherRect.left();
        int otherRight = otherRect.right() + 1; // Qt的right()是width()-1
        int otherTop = otherRect.top();
        int otherBottom = otherRect.bottom() + 1;
        
        // 水平方向吸附检测
        // 左边缘对齐其他围栏的左边缘
        int dist = qAbs(myLeft - otherLeft);
        if (dist < snapX && dist <= SNAP_THRESHOLD) {
            snapX = dist;
            snappedPos.setX(otherLeft);
        }
        // 左边缘对齐其他围栏的右边缘
        dist = qAbs(myLeft - otherRight);
        if (dist < snapX && dist <= SNAP_THRESHOLD) {
            snapX = dist;
            snappedPos.setX(otherRight);
        }
        // 右边缘对齐其他围栏的左边缘
        dist = qAbs(myRight - otherLeft);
        if (dist < snapX && dist <= SNAP_THRESHOLD) {
            snapX = dist;
            snappedPos.setX(otherLeft - targetSize.width());
        }
        // 右边缘对齐其他围栏的右边缘
        dist = qAbs(myRight - otherRight);
        if (dist < snapX && dist <= SNAP_THRESHOLD) {
            snapX = dist;
            snappedPos.setX(otherRight - targetSize.width());
        }
        
        // 垂直方向吸附检测
        // 上边缘对齐其他围栏的上边缘
        dist = qAbs(myTop - otherTop);
        if (dist < snapY && dist <= SNAP_THRESHOLD) {
            snapY = dist;
            snappedPos.setY(otherTop);
        }
        // 上边缘对齐其他围栏的下边缘
        dist = qAbs(myTop - otherBottom);
        if (dist < snapY && dist <= SNAP_THRESHOLD) {
            snapY = dist;
            snappedPos.setY(otherBottom);
        }
        // 下边缘对齐其他围栏的上边缘
        dist = qAbs(myBottom - otherTop);
        if (dist < snapY && dist <= SNAP_THRESHOLD) {
            snapY = dist;
            snappedPos.setY(otherTop - targetSize.height());
        }
        // 下边缘对齐其他围栏的下边缘
        dist = qAbs(myBottom - otherBottom);
        if (dist < snapY && dist <= SNAP_THRESHOLD) {
            snapY = dist;
            snappedPos.setY(otherBottom - targetSize.height());
        }
    }
    
    return snappedPos;
}

// 边缘吸附：调整大小时计算吸附后的几何矩形
QRect FenceWindow::snapGeometryToOtherFences(const QRect& targetGeo, int resizeEdge) const
{
    QRect snappedGeo = targetGeo;
    
    // 遍历所有其他围栏
    for (FenceWindow* other : s_allFences) {
        if (other == this) continue;
        
        QRect otherRect = other->geometry();
        int otherLeft = otherRect.left();
        int otherRight = otherRect.right() + 1;
        int otherTop = otherRect.top();
        int otherBottom = otherRect.bottom() + 1;
        
        // 根据调整的边缘进行吸附
        if (resizeEdge & Left) {
            int myLeft = snappedGeo.left();
            // 左边缘对齐其他围栏的左边缘
            if (qAbs(myLeft - otherLeft) <= SNAP_THRESHOLD) {
                snappedGeo.setLeft(otherLeft);
            }
            // 左边缘对齐其他围栏的右边缘
            else if (qAbs(myLeft - otherRight) <= SNAP_THRESHOLD) {
                snappedGeo.setLeft(otherRight);
            }
        }
        
        if (resizeEdge & Right) {
            int myRight = snappedGeo.right() + 1;
            // 右边缘对齐其他围栏的左边缘
            if (qAbs(myRight - otherLeft) <= SNAP_THRESHOLD) {
                snappedGeo.setRight(otherLeft - 1);
            }
            // 右边缘对齐其他围栏的右边缘
            else if (qAbs(myRight - otherRight) <= SNAP_THRESHOLD) {
                snappedGeo.setRight(otherRight - 1);
            }
        }
        
        if (resizeEdge & Top) {
            int myTop = snappedGeo.top();
            // 上边缘对齐其他围栏的上边缘
            if (qAbs(myTop - otherTop) <= SNAP_THRESHOLD) {
                snappedGeo.setTop(otherTop);
            }
            // 上边缘对齐其他围栏的下边缘
            else if (qAbs(myTop - otherBottom) <= SNAP_THRESHOLD) {
                snappedGeo.setTop(otherBottom);
            }
        }
        
        if (resizeEdge & Bottom) {
            int myBottom = snappedGeo.bottom() + 1;
            // 下边缘对齐其他围栏的上边缘
            if (qAbs(myBottom - otherTop) <= SNAP_THRESHOLD) {
                snappedGeo.setBottom(otherTop - 1);
            }
            // 下边缘对齐其他围栏的下边缘
            else if (qAbs(myBottom - otherBottom) <= SNAP_THRESHOLD) {
                snappedGeo.setBottom(otherBottom - 1);
            }
        }
    }
    
    return snappedGeo;
}

void FenceWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
        m_dragStartGlobalPos = event->globalPos();
        m_resizeStartSize = size();
        m_resizeStartGeo = geometry();
        
        // 边缘检测
        m_resizeEdge = None;
        int x = event->x();
        int y = event->y();
        int w = width();
        int h = height();
        int border = 8;
        
        // 折叠状态下只允许调整左右宽度
        if (m_collapsed) {
             if (x < border) m_resizeEdge |= Left;
             if (x > w - border) m_resizeEdge |= Right;
        } else {
             if (x < border) m_resizeEdge |= Left;
             if (x > w - border) m_resizeEdge |= Right;
             if (y < border) m_resizeEdge |= Top;
             if (y > h - border) m_resizeEdge |= Bottom;
        }

        if (m_resizeEdge != None) {
            m_isResizing = true;
            
            // 设置全局光标，防止拖拽过程中光标丢失
            Qt::CursorShape shape = Qt::ArrowCursor;
            if ((m_resizeEdge & Left && m_resizeEdge & Top) || (m_resizeEdge & Right && m_resizeEdge & Bottom)) {
                shape = Qt::SizeFDiagCursor;
            } else if ((m_resizeEdge & Right && m_resizeEdge & Top) || (m_resizeEdge & Left && m_resizeEdge & Bottom)) {
                shape = Qt::SizeBDiagCursor;
            } else if (m_resizeEdge & (Left | Right)) {
                shape = Qt::SizeHorCursor;
            } else if (m_resizeEdge & (Top | Bottom)) {
                shape = Qt::SizeVerCursor;
            }
            QApplication::setOverrideCursor(QCursor(shape));
            
        } else if (titleBarRect().contains(event->pos())) {
            // 只允许标题栏区域拖动
            m_isDragging = true;
        }
        
        // 注意：移除了 raise() 调用，因为桌面嵌入窗口不应该改变 Z-order
        // 这可能会干扰 WM_WINDOWPOSCHANGING 拦截逻辑
        event->accept();
    }
}

void FenceWindow::mouseMoveEvent(QMouseEvent *event)
{
    // 如果正在拖拽
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->globalPos() - m_dragStartGlobalPos;
        QPoint newPos = m_resizeStartGeo.topLeft() + delta; // 使用起始几何位置
        
        // 应用围栏间边缘吸附
        QPoint snappedPos = snapPositionToOtherFences(newPos, size());
        
        move(snappedPos);
        // 注意：不更新 m_dragStartGlobalPos，保持相对于起始位置的计算
        event->accept();
        return;
    }
    
    // 如果正在调整大小
    if (m_isResizing && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->globalPos() - m_dragStartGlobalPos;
        QRect newGeo = m_resizeStartGeo;
        
        if (m_resizeEdge & Left) {
            newGeo.setLeft(m_resizeStartGeo.left() + delta.x());
            if (newGeo.width() < minimumWidth()) newGeo.setLeft(m_resizeStartGeo.right() - minimumWidth());
        }
        if (m_resizeEdge & Right) {
            newGeo.setRight(m_resizeStartGeo.right() + delta.x());
            if (newGeo.width() < minimumWidth()) newGeo.setRight(m_resizeStartGeo.left() + minimumWidth());
        }
        if (m_resizeEdge & Top) {
            newGeo.setTop(m_resizeStartGeo.top() + delta.y());
            if (newGeo.height() < minimumHeight()) newGeo.setTop(m_resizeStartGeo.bottom() - minimumHeight());
        }
        if (m_resizeEdge & Bottom) {
            newGeo.setBottom(m_resizeStartGeo.bottom() + delta.y());
            if (newGeo.height() < minimumHeight()) newGeo.setBottom(m_resizeStartGeo.top() + minimumHeight());
        }
        
        // 应用围栏间边缘吸附
        newGeo = snapGeometryToOtherFences(newGeo, m_resizeEdge);
        
        // 确保吸附后仍满足最小尺寸约束
        if (newGeo.width() < minimumWidth() || newGeo.height() < minimumHeight()) {
            // 如果吸附导致尺寸过小，恢复到吸附前的几何
            newGeo = m_resizeStartGeo;
            if (m_resizeEdge & Left) {
                newGeo.setLeft(m_resizeStartGeo.left() + delta.x());
                if (newGeo.width() < minimumWidth()) newGeo.setLeft(m_resizeStartGeo.right() - minimumWidth());
            }
            if (m_resizeEdge & Right) {
                newGeo.setRight(m_resizeStartGeo.right() + delta.x());
                if (newGeo.width() < minimumWidth()) newGeo.setRight(m_resizeStartGeo.left() + minimumWidth());
            }
            if (m_resizeEdge & Top) {
                newGeo.setTop(m_resizeStartGeo.top() + delta.y());
                if (newGeo.height() < minimumHeight()) newGeo.setTop(m_resizeStartGeo.bottom() - minimumHeight());
            }
            if (m_resizeEdge & Bottom) {
                newGeo.setBottom(m_resizeStartGeo.bottom() + delta.y());
                if (newGeo.height() < minimumHeight()) newGeo.setBottom(m_resizeStartGeo.top() + minimumHeight());
            }
        }
        
        setGeometry(newGeo);
        event->accept();
        return;
    }

    // 更新鼠标光标形状
    int x = event->x();
    int y = event->y();
    int w = width();
    int h = height();
    int border = 8;
    int edge = None;
    
    // 折叠状态下只检测左右边缘
    if (m_collapsed) {
         if (x < border) edge |= Left;
         if (x > w - border) edge |= Right;
    } else {
         if (x < border) edge |= Left;
         if (x > w - border) edge |= Right;
         if (y < border) edge |= Top;
         if (y > h - border) edge |= Bottom;
    }
    
    if ((edge & Left && edge & Top) || (edge & Right && edge & Bottom)) {
        setCursor(Qt::SizeFDiagCursor);
    } else if ((edge & Right && edge & Top) || (edge & Left && edge & Bottom)) {
        setCursor(Qt::SizeBDiagCursor);
    } else if (edge & (Left | Right)) {
        setCursor(Qt::SizeHorCursor);
    } else if (edge & (Top | Bottom)) {
        setCursor(Qt::SizeVerCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }

    // 如果没有拖拽或调整大小，传递给子控件
    event->ignore();
}

void FenceWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_isDragging || m_isResizing) {
        if (m_isResizing) {
            QApplication::restoreOverrideCursor();
        }
        
        // 拖动结束后，确保围栏完全在屏幕内（允许贴边）
        if (m_isDragging) {
            QScreen *screen = QApplication::screenAt(geometry().center());
            if (!screen) {
                screen = QApplication::primaryScreen();
            }
            if (screen) {
                QRect screenRect = screen->availableGeometry();
                QPoint newPos = pos();
                
                // 限制窗口不能超出屏幕边缘（允许完全贴边）
                // 左边界：窗口左边缘不能小于屏幕左边缘
                if (newPos.x() < screenRect.left()) {
                    newPos.setX(screenRect.left());
                }
                // 右边界：窗口右边缘不能超过屏幕右边缘
                if (newPos.x() + width() > screenRect.right()) {
                    newPos.setX(screenRect.right() - width());
                }
                // 上边界：窗口上边缘不能小于屏幕上边缘
                if (newPos.y() < screenRect.top()) {
                    newPos.setY(screenRect.top());
                }
                // 下边界：窗口下边缘不能超过屏幕下边缘
                if (newPos.y() + height() > screenRect.bottom()) {
                    newPos.setY(screenRect.bottom() - height());
                }
                
                if (newPos != pos()) {
                    move(newPos);
                }
            }
        }
        
        m_isDragging = false;
        m_isResizing = false;
        
        // 仅拖动时保存位置，调整大小时 geometryChanged 信号会在 resizeEvent 中触发吗？不一定，手动 setGeometry 会触发
        // 为了保险，统一在释放时触发一次保存
        emit geometryChanged(); // 保存位置和大小
    }
    
    // 恢复光标（如果在边缘但松开了鼠标，保持调整光标，移出由 mouseMove 处理）
    // 但如果拖动结束，可能需要强制更新一次
    event->accept();
}

void FenceWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && titleBarRect().contains(event->pos())) {
        // 双击标题栏：编辑标题
        startTitleEdit();
    }
}



void FenceWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    // 移动时重置定时器，停止移动1秒后触发保存
    if (m_saveTimer) m_saveTimer->start();
}


void FenceWindow::enterEvent(QEvent *event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void FenceWindow::leaveEvent(QEvent *event)
{
    m_hovered = false;
    setCursor(Qt::ArrowCursor);
    update();
    QWidget::leaveEvent(event);
}

void FenceWindow::hideEvent(QHideEvent *event)
{
    // 如果不是正在关闭且不是用户主动隐藏，则阻止隐藏
    if (!m_isClosing && !m_userHidden) {
        event->ignore();
        show(); // 强制保持显示
    } else {
        QWidget::hideEvent(event);
    }
}

void FenceWindow::closeEvent(QCloseEvent *event)
{
    m_isClosing = true;
    QWidget::closeEvent(event);
}

void FenceWindow::changeEvent(QEvent *event)
{
    // 拦截窗口状态变化，防止被最小化
    if (event->type() == QEvent::WindowStateChange) {
        if (windowState() & Qt::WindowMinimized) {
            // 如果窗口被最小化，立即恢复正常状态
            setWindowState(windowState() & ~Qt::WindowMinimized);
            show(); // 确保窗口显示
            event->ignore();
            return;
        }
    }
    QWidget::changeEvent(event);
}

void FenceWindow::setAlwaysOnTop(bool onTop)
{
    if (m_alwaysOnTop == onTop) return;
    
    m_alwaysOnTop = onTop;
    
#ifdef Q_OS_WIN
    HWND hWnd = (HWND)winId();
    
    if (onTop) {
        // 设置为始终置顶
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, 
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        // 取消置顶，放回桌面图标层上面
        HWND hProgman = FindWindow(L"Progman", NULL);
        HWND hDefView = FindWindowEx(hProgman, NULL, L"SHELLDLL_DefView", NULL);
        
        if (!hDefView) {
            HWND hWorkerW = NULL;
            while ((hWorkerW = FindWindowEx(NULL, hWorkerW, L"WorkerW", NULL)) != NULL) {
                 hDefView = FindWindowEx(hWorkerW, NULL, L"SHELLDLL_DefView", NULL);
                 if (hDefView) break;
            }
        }
        
        HWND hListView = NULL;
        if (hDefView) {
            hListView = FindWindowEx(hDefView, NULL, L"SysListView32", NULL);
        }
        
        if (hListView) {
            SetWindowPos(hWnd, hListView, 0, 0, 0, 0, 
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else {
            SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0, 
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
#endif
    
    emit geometryChanged(); // 触发保存
}

void FenceWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = new QMenu(this);
    // 启用透明背景，让 Qt 绘制抗锯齿圆角
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setAttribute(Qt::WA_NoSystemBackground);
    // 给边框留出 1px 的边距，防止边缘毛刺
    menu->setContentsMargins(1, 1, 1, 1);
    
    // 使用 standard Popup 保证原生菜单体验
    menu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    
    // 使用半透明背景 (rgba)，Qt 会自动处理抗锯齿圆角
    menu->setStyleSheet(R"(
        QMenu {
            background-color: rgba(30, 30, 35, 200);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 8px;
            padding: 4px;
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
        }
        QMenu::item {
            background: transparent;
            color: #ffffff;
            padding: 5px 16px;
            min-height: 20px;
            border-radius: 4px;
            margin: 2px 4px;
        }
        QMenu::item:selected {
            background-color: rgba(255, 255, 255, 0.1);
        }
        QMenu::separator {
            height: 1px;
            background: rgba(255, 255, 255, 0.1);
            margin: 4px 8px;
        }
    )");

    QAction *renameAction = menu->addAction("重命名");
    QAction *collapseAction = menu->addAction(m_collapsed ? "展开" : "折叠");
    menu->addSeparator();
    QAction *deleteAction = menu->addAction("删除围栏");

    // 连接菜单动作
    connect(renameAction, &QAction::triggered, this, &FenceWindow::startTitleEdit);
    connect(collapseAction, &QAction::triggered, this, [this]() {
        setCollapsed(!m_collapsed);
    });
    connect(deleteAction, &QAction::triggered, this, [this]() {
        emit deleteRequested(this);
    });

#ifdef Q_OS_WIN
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() {
        // 使用定时器延迟执行，确保在 Qt 完成窗口创建和显示之后再进行 Hack
        // 这样可以覆盖 Qt 默认的 TopMost 设置
        QTimer::singleShot(10, this, [this, menu]() {
            if (!menu) return;
            HWND hMenu = (HWND)menu->winId();
            HWND hFence = (HWND)this->winId();
            
            // 0. 裁剪圆角 (解决黑点/毛刺问题)
            BlurHelper::enableRoundedCorners(menu, 8); 
            
            // 1. 移除 WS_EX_TOPMOST 扩展样式，防止强制置顶

            // 1. 移除 WS_EX_TOPMOST 扩展样式，防止强制置顶
            LONG_PTR exStyle = GetWindowLongPtr(hMenu, GWL_EXSTYLE);
            if (exStyle & WS_EX_TOPMOST) {
                SetWindowLongPtr(hMenu, GWL_EXSTYLE, exStyle & ~WS_EX_TOPMOST);
            }
            
            // 2. 调整 Z-order
            // 获取围栏上面的那个窗口（hPrev）
            // 如果我们将菜单插入到 hPrev 后面，菜单自然就在 围栏 上面，且在 hPrev 下面
            HWND hPrev = GetWindow(hFence, GW_HWNDPREV);
            
            UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED;
            
            if (hPrev) {
                SetWindowPos(hMenu, hPrev, 0, 0, 0, 0, flags);
            } else {
                // 如果围栏上面没有窗口（围栏就是当前层级的顶），则将菜单设为顶
                // 注意：由于移除了 TOPMOST，这里的 HWND_TOP 只是非 TopMost 窗口的顶
                SetWindowPos(hMenu, HWND_TOP, 0, 0, 0, 0, flags);
            }
        });
    });
#endif

    // 使用 exec 阻塞显示
    menu->exec(event->globalPos());
}







void FenceWindow::dragEnterEvent(QDragEnterEvent *event)
{
    logToDesktop("[dragEnterEvent] Fence: " + m_title);
    logToDesktop("  hasFormat(x-deskgo-icon): " + QString(event->mimeData()->hasFormat("application/x-deskgo-icon") ? "true" : "false"));
    logToDesktop("  hasUrls: " + QString(event->mimeData()->hasUrls() ? "true" : "false"));
    
    // 接受内部图标拖放和外部文件拖放
    if (event->mimeData()->hasFormat("application/x-deskgo-icon") || event->mimeData()->hasUrls()) {
        logToDesktop("  -> Accepted!");
        event->acceptProposedAction();
        m_hovered = true;
        update();
    } else {
        logToDesktop("  -> Rejected (no matching format)");
    }
}

void FenceWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-deskgo-icon") || event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        
        // 如果是内部图标拖拽，显示插入位置指示器
        if (event->mimeData()->hasFormat("application/x-deskgo-icon")) {
            QString iconPath = QString::fromUtf8(event->mimeData()->data("application/x-deskgo-icon"));
            
            // 查找当前拖拽的图标在本围栏中的索引
            int draggedIconIndex = -1;
            for (int i = 0; i < m_icons.size(); ++i) {
                if (m_icons[i]->path() == iconPath) {
                    draggedIconIndex = i;
                    break;
                }
            }
            
            QPoint dropPos = event->pos();
            QPoint contentPos = m_contentArea->mapFrom(this, dropPos);
            
            // 计算插入位置
            int targetIndex = m_icons.size(); // 默认末尾
            QRect indicatorRect;
            
            for (int i = 0; i < m_icons.size(); ++i) {
                IconWidget *icon = m_icons[i];
                QRect iconRect = icon->geometry();
                
                // 如果鼠标在图标左半部分
                if (contentPos.x() < iconRect.center().x()) {
                    if (contentPos.y() < iconRect.bottom() + 20) {
                        targetIndex = i;
                        // 指示器位置在该图标左侧
                        indicatorRect = QRect(
                            iconRect.left() - 1,
                            iconRect.top(),
                            2,
                            iconRect.height()
                        );
                        break;
                    }
                }
            }
            
            // 如果没有找到合适位置（放到末尾）
            if (targetIndex == m_icons.size() && !m_icons.isEmpty()) {
                IconWidget *lastIcon = m_icons.last();
                QRect lastRect = lastIcon->geometry();
                indicatorRect = QRect(
                    lastRect.right() + 1,
                    lastRect.top(),
                    2,
                    lastRect.height()
                );
            }
            
            // 如果是同一围栏内拖拽，检查是否拖到无效位置
            if (draggedIconIndex != -1) {
                // 首位图标不能再往前拖（targetIndex == 0 且 draggedIconIndex == 0）
                // 末位图标不能再往后拖（targetIndex == size 且 draggedIconIndex == size-1）
                // 拖到自己当前位置或紧邻后面位置也不显示指示器
                if ((draggedIconIndex == 0 && targetIndex == 0) ||
                    (draggedIconIndex == m_icons.size() - 1 && targetIndex == m_icons.size()) ||
                    (targetIndex == draggedIconIndex || targetIndex == draggedIconIndex + 1)) {
                    // 不显示指示器
                    m_showDropIndicator = false;
                    update();
                    return;
                }
            }
            
            m_showDropIndicator = !indicatorRect.isNull();
            m_dropIndicatorIndex = targetIndex;
            m_dropIndicatorRect = indicatorRect;
            update(); // 触发重绘
        }
    }
}

void FenceWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event)
    m_hovered = false;
    m_showDropIndicator = false;
    update();
}


// 移除这里的 getWinIcon 定义，移到头部
void FenceWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    logToDesktop("[dropEvent] Fence: " + m_title);
    logToDesktop("  hasFormat(x-deskgo-icon): " + QString(mimeData->hasFormat("application/x-deskgo-icon") ? "true" : "false"));
    logToDesktop("  hasUrls: " + QString(mimeData->hasUrls() ? "true" : "false"));
    
    // 处理内部图标拖放（围栏之间移动图标）
    if (mimeData->hasFormat("application/x-deskgo-icon")) {
        QString iconPath = QString::fromUtf8(mimeData->data("application/x-deskgo-icon"));
        logToDesktop("  iconPath: " + iconPath);
        
        // 检查图标是否在当前围栏（同围栏内排序）
        IconWidget *existingIcon = nullptr;
        int existingIndex = -1;
        for (int i = 0; i < m_icons.size(); ++i) {
            if (m_icons[i]->path() == iconPath) {
                existingIcon = m_icons[i];
                existingIndex = i;
                break;
            }
        }
        
        if (existingIcon) {
            // 图标在同一围栏内，执行拖拽排序
            QPoint dropPos = event->pos();
            QPoint contentPos = m_contentArea->mapFrom(this, dropPos);
            
            logToDesktop("  Same fence reorder - dropPos: " + QString::number(contentPos.x()) + "," + QString::number(contentPos.y()));
            
            // 计算目标插入位置
            int targetIndex = m_icons.size(); // 默认放到末尾
            
            for (int i = 0; i < m_icons.size(); ++i) {
                IconWidget *icon = m_icons[i];
                QRect iconRect = icon->geometry();
                
                // 如果放置位置在图标的左半部分，插入到该位置之前
                if (contentPos.x() < iconRect.center().x()) {
                    // 检查 Y 坐标是否在同一行或之前的行
                    if (contentPos.y() < iconRect.bottom() + 20) {
                        targetIndex = i;
                        break;
                    }
                }
            }
            
            logToDesktop("  existingIndex: " + QString::number(existingIndex) + " targetIndex: " + QString::number(targetIndex));
            
            // 如果需要移动
            if (targetIndex != existingIndex && targetIndex != existingIndex + 1) {
                FlowLayout *flowLayout = dynamic_cast<FlowLayout*>(m_contentLayout);
                
                // 从当前位置移除
                m_icons.removeAt(existingIndex);
                if (flowLayout) {
                    int layoutIdx = flowLayout->indexOf(existingIcon);
                    if (layoutIdx >= 0) {
                        flowLayout->takeAt(layoutIdx);
                    }
                }
                
                // 调整目标索引
                if (existingIndex < targetIndex) {
                    targetIndex--;
                }
                
                // 插入到新位置
                if (targetIndex >= m_icons.size()) {
                    m_icons.append(existingIcon);
                    if (flowLayout) {
                        flowLayout->addItem(new QWidgetItem(existingIcon));
                    }
                } else {
                    m_icons.insert(targetIndex, existingIcon);
                    if (flowLayout) {
                        flowLayout->insertItem(targetIndex, new QWidgetItem(existingIcon));
                    }
                }
                
                // 强制重新布局
                m_contentLayout->invalidate();
                m_contentArea->updateGeometry();
                m_contentArea->update();
                
                emit geometryChanged();
                logToDesktop("  Reorder completed!");
            }
            
            event->acceptProposedAction();
            m_hovered = false;
            m_showDropIndicator = false;  // 清除插入位置指示器
            update();
            return;
        }
        
        // 通过 FenceManager 查找源围栏和图标
        // 先获取拖拽源（IconWidget 的父围栏）
        QObject *source = event->source();
        logToDesktop("  event->source(): " + QString(source ? source->metaObject()->className() : "nullptr"));
        IconWidget *sourceIcon = qobject_cast<IconWidget*>(source);
        logToDesktop("  sourceIcon: " + QString(sourceIcon ? "found" : "nullptr"));
        
        if (sourceIcon) {
            // 找到源围栏
            FenceWindow *sourceFence = nullptr;
            QWidget *parent = sourceIcon->parentWidget();
            logToDesktop("  Looking for source fence...");
            while (parent) {
                logToDesktop("    parent: " + QString(parent->metaObject()->className()));
                sourceFence = qobject_cast<FenceWindow*>(parent);
                if (sourceFence) break;
                parent = parent->parentWidget();
            }
            logToDesktop("  sourceFence: " + QString(sourceFence ? sourceFence->title() : "nullptr"));
            
            if (sourceFence && sourceFence != this) {
                // 保存图标数据
                IconWidget::IconData data = sourceIcon->data();
                
                // 从源围栏移除（不删除文件，只从列表和布局中移除）
                sourceFence->m_icons.removeOne(sourceIcon);
                sourceFence->m_contentLayout->removeWidget(sourceIcon);
                
                // 移动文件到新围栏的存储目录
                QString storagePath = QCoreApplication::applicationDirPath() + "/fences_storage/" + m_id;
                storagePath = QDir::toNativeSeparators(QDir::cleanPath(storagePath));
                QDir().mkpath(storagePath);
                
                QFileInfo fileInfo(data.path);
                QString newPath = storagePath + QDir::separator() + fileInfo.fileName();
                newPath = QDir::toNativeSeparators(QDir::cleanPath(newPath));
                
                // 如果文件在其他围栏的存储目录中，移动到新位置
                if (data.path != newPath && QFile::exists(data.path)) {
                    if (QFile::exists(newPath)) {
                        QFile::remove(newPath);
                    }
                    if (QFile::rename(data.path, newPath)) {
                        data.path = newPath;
                        data.targetPath = newPath;
                    }
                }
                
                // 销毁旧的 IconWidget
                sourceIcon->deleteLater();
                
                // 创建新的 IconWidget 并添加到当前围栏
                IconWidget *newIcon = new IconWidget(data);
                addIcon(newIcon);
                
                emit sourceFence->geometryChanged();  // 触发源围栏保存
                emit geometryChanged();  // 触发当前围栏保存
            }
        }
        
        event->acceptProposedAction();
        m_hovered = false;
        update();
        return;
    }
    
    // 处理外部文件拖放
    if (mimeData->hasUrls()) {
        qDebug() << "[dropEvent] Processing URLs...";
        qDebug() << "  URL count:" << mimeData->urls().count();
        
        QFileIconProvider iconProvider;
        
        // 确保存储目录存在
        // 使用程序目录存储被移动的文件
        QString storagePath = QCoreApplication::applicationDirPath() + "/fences_storage/" + m_id;
        storagePath = QDir::toNativeSeparators(QDir::cleanPath(storagePath));
        qDebug() << "  storagePath:" << storagePath;
        QDir().mkpath(storagePath);

        QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        qDebug() << "  desktopPath:" << desktopPath;
        
        for (const QUrl &url : mimeData->urls()) {
            qDebug() << "  Processing URL:" << url;
            if (url.isLocalFile()) {
                QString srcPath = QDir::toNativeSeparators(url.toLocalFile());
                qDebug() << "    srcPath:" << srcPath;
                QFileInfo fileInfo(srcPath);
                
                bool isDesktopFile = QFileInfo(srcPath).absolutePath().compare(desktopPath, Qt::CaseInsensitive) == 0 ||
                                     srcPath.contains("Desktop", Qt::CaseInsensitive);
                qDebug() << "    isDesktopFile:" << isDesktopFile;

                QPoint originalPos(-1, -1);
                QString targetPath = srcPath;
                bool moved = false;

                if (isDesktopFile) {
                    originalPos = DesktopHelper::getIconPosition(srcPath);
                    qDebug() << "    originalPos:" << originalPos;
                    
                    // 2. 移动文件到存储目录
                    QString newPath = storagePath + QDir::separator() + fileInfo.fileName();
                    newPath = QDir::toNativeSeparators(QDir::cleanPath(newPath));
                    qDebug() << "    newPath:" << newPath;
                    
                    // 处理重名
                    if (QFile::exists(newPath)) {
                        qDebug() << "    File exists, removing...";
                        QFile::remove(newPath);
                    }

                    qDebug() << "    Attempting to move file...";
                    if (QFile::rename(srcPath, newPath)) {
                        targetPath = newPath;
                        moved = true;
                        qDebug() << "    Move SUCCESS!";
                        // 使用精确的文件通知，刷新更快
                        DesktopHelper::notifyFileRemoved(srcPath);
                    } else {
                        // 移动失败！
                        qDebug() << "    Move FAILED!";
                        QMessageBox::critical(this, "拥有权错误", "无法移动文件: " + srcPath + "\n可能是权限不足或文件被占用。");
                        continue; // 跳过此文件，不添加图标
                    }
                } else {
                    qDebug() << "    Not a desktop file, keeping at original location";
                }
                
                // 创建图标数据
                IconWidget::IconData data;
                data.name = fileInfo.completeBaseName();
                data.path = targetPath;
                data.targetPath = targetPath;
                qDebug() << "    Creating icon with name:" << data.name << "path:" << data.path;
                
                // 优先使用 WinAPI 获取图标
                data.icon = getWinIcon(targetPath);
                qDebug() << "    WinAPI icon isNull:" << data.icon.isNull();
                
                if (data.icon.isNull()) {
                    data.icon = iconProvider.icon(QFileInfo(targetPath)).pixmap(48, 48);
                    qDebug() << "    FileIconProvider icon isNull:" << data.icon.isNull();
                }
                
                if (data.icon.isNull()) {
                     data.icon = iconProvider.icon(QFileIconProvider::File).pixmap(48, 48);
                     qDebug() << "    Using default file icon";
                }

                data.originalPosition = originalPos;
                data.isFromDesktop = moved;
                
                IconWidget *iconWidget = new IconWidget(data);
                qDebug() << "    Calling addIcon()...";
                addIcon(iconWidget);
                qDebug() << "    Icon added! Current icon count:" << m_icons.count();
            } else {
                qDebug() << "    URL is not local file, skipping";
            }
        }
        
        event->acceptProposedAction();
        emit geometryChanged();  // 触发保存
    }
    
    m_hovered = false;
    m_showDropIndicator = false;  // 清除插入位置指示器
    update();
}

void FenceWindow::startTitleEdit()
{
    if (m_titleEdit) return;  // 已经在编辑中
    
    // 获取标题标签的实际位置
    QRect labelGeom = m_titleLabel->geometry();
    
    // 清空标题标签的文字（保持占位，不影响布局）
    m_titleLabel->setText("");
    
    // 创建编辑框，放在标题标签的精确位置
    m_titleEdit = new QLineEdit(m_title, this);
    m_titleEdit->setGeometry(labelGeom);
    m_titleEdit->setAlignment(Qt::AlignCenter);
    m_titleEdit->setStyleSheet(R"(
        QLineEdit {
            color: #ffffff;
            font-size: 12px;
            font-weight: 500;
            background: transparent;
            border: none;
            border-bottom: 2px solid rgba(100, 150, 255, 0.8);
            padding: 0 10px;
        }
    )");
    m_titleEdit->selectAll();
    m_titleEdit->show();
    m_titleEdit->raise();
    
    // 使用 Qt 的方式设置焦点，但不激活窗口
    m_titleEdit->setFocus(Qt::MouseFocusReason);
    
    // 按 Enter 完成编辑
    connect(m_titleEdit, &QLineEdit::returnPressed, this, &FenceWindow::finishTitleEdit);
    
    // 安装事件过滤器处理 ESC 键和焦点丢失
    m_titleEdit->installEventFilter(this);
    
    // 安装全局事件过滤器，监听应用程序级别的鼠标点击
    qApp->installEventFilter(this);
    
#ifdef Q_OS_WIN
    // 安装鼠标钩子以检测桌面点击
    if (!s_hMouseHook) {
        s_editingFence = this;
        s_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, GetModuleHandle(NULL), 0);
        if (s_hMouseHook) {
            qDebug() << "[startTitleEdit] Mouse hook installed";
        } else {
            qDebug() << "[startTitleEdit] Failed to install mouse hook, error:" << GetLastError();
        }
    }
#endif
}

void FenceWindow::finishTitleEdit()
{
    if (!m_titleEdit) return;
    
    // 移除全局事件过滤器
    qApp->removeEventFilter(this);
    
#ifdef Q_OS_WIN
    // 卸载鼠标钩子
    if (s_hMouseHook && s_editingFence.data() == this) {
        UnhookWindowsHookEx(s_hMouseHook);
        s_hMouseHook = NULL;
        s_editingFence.clear();
        qDebug() << "[finishTitleEdit] Mouse hook uninstalled";
    }
#endif
    
    // 获取新标题
    QString newTitle = m_titleEdit->text().trimmed();
    if (!newTitle.isEmpty() && newTitle != m_title) {
        setTitle(newTitle);
        emit geometryChanged();  // 触发保存
    }
    
    // 移除事件过滤器并删除
    m_titleEdit->removeEventFilter(this);
    m_titleEdit->deleteLater();
    m_titleEdit = nullptr;
    
    // 恢复标题标签的文字
    m_titleLabel->setText(m_title);
    
#ifdef Q_OS_WIN
    // 编辑完成后，重新设置 Z-order
    QTimer::singleShot(10, this, [this]() {
        HWND hWnd = (HWND)winId();
        
        // 找到桌面图标层
        HWND hProgman = FindWindow(L"Progman", NULL);
        HWND hDefView = FindWindowEx(hProgman, NULL, L"SHELLDLL_DefView", NULL);
        
        if (!hDefView) {
            HWND hWorkerW = NULL;
            while ((hWorkerW = FindWindowEx(NULL, hWorkerW, L"WorkerW", NULL)) != NULL) {
                 hDefView = FindWindowEx(hWorkerW, NULL, L"SHELLDLL_DefView", NULL);
                 if (hDefView) break;
            }
        }
        
        HWND hListView = NULL;
        if (hDefView) {
            hListView = FindWindowEx(hDefView, NULL, L"SysListView32", NULL);
        }
        
        if (hListView) {
            // 先降到最底层
            SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0, 
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            
            // 然后提升到桌面图标上方
            SetWindowPos(hWnd, hListView, 0, 0, 0, 0, 
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            
            qDebug() << "[finishTitleEdit] Z-order reset after editing";
        }
    });
#endif
}

bool FenceWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 只处理当前窗口及其子控件的事件
    if (event->type() == QEvent::MouseMove) {
        // 如果正在调整大小或拖拽，不需要额外处理光标，交由 mouseMoveEvent 处理
        if (!m_isResizing && !m_isDragging) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            QWidget *widget = qobject_cast<QWidget*>(watched);
            // 只处理当前窗口的子控件
            if (widget && widget->window() == this) {
                // 将子控件坐标转换为窗口坐标
                QPoint pos = widget->mapTo(this, mouseEvent->pos());
                
                // 更新鼠标光标形状
                int x = pos.x();
                int y = pos.y();
                int w = width();
                int h = height();
                int border = 8;
                int edge = None;
                
                // 折叠状态下只检测左右边缘
                if (m_collapsed) {
                     if (x < border) edge |= Left;
                     if (x > w - border) edge |= Right;
                } else {
                     if (x < border) edge |= Left;
                     if (x > w - border) edge |= Right;
                     if (y < border) edge |= Top;
                     if (y > h - border) edge |= Bottom;
                }
                
                if (edge != None) {
                    if ((edge & Left && edge & Top) || (edge & Right && edge & Bottom)) {
                        setCursor(Qt::SizeFDiagCursor);
                    } else if ((edge & Right && edge & Top) || (edge & Left && edge & Bottom)) {
                        setCursor(Qt::SizeBDiagCursor);
                    } else if (edge & (Left | Right)) {
                        setCursor(Qt::SizeHorCursor);
                    } else if (edge & (Top | Bottom)) {
                        setCursor(Qt::SizeVerCursor);
                    }
                } else {
                    // 恢复默认
                    setCursor(Qt::ArrowCursor);
                }
            }
        }
    }

    if (watched == m_titleEdit) {
        // 处理 ESC 键：取消编辑
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
#ifdef Q_OS_WIN
                // 卸载鼠标钩子
                if (s_hMouseHook && s_editingFence.data() == this) {
                    UnhookWindowsHookEx(s_hMouseHook);
                    s_hMouseHook = NULL;
                    s_editingFence.clear();
                }
#endif
                m_titleEdit->removeEventFilter(this);
                m_titleEdit->deleteLater();
                m_titleEdit = nullptr;
                m_titleLabel->setText(m_title);  // 恢复原标题
                
                // 移除全局事件过滤器
                qApp->removeEventFilter(this);
                return true;
            }
        }
        // 处理焦点丢失：保存并关闭
        if (event->type() == QEvent::FocusOut) {
            QTimer::singleShot(0, this, &FenceWindow::finishTitleEdit);
            return false;
        }
    }
    return QWidget::eventFilter(watched, event);
}

