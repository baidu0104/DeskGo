#include "fencewindow.h"
#include "iconwidget.h"
#include "flowlayout.h"
#include "../platform/blurhelper.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QMenu>
#include <QInputDialog>
#include <QJsonObject>
#include <QJsonArray>
#include <QScreen>
#include <QApplication>
#include <QMimeData>
#include <QVariantAnimation>
#include <QDragEnterEvent>
#include <QDropEvent>
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
#include <dwmapi.h>
#endif

#include <QtWin>
#include <QtWinExtras>

// 日志记录函数
void logToDesktop(const QString &msg) {
    Q_UNUSED(msg);
    // 调试日志已禁用
}

// 获取系统原生图标辅助函数
static QPixmap getWinIcon(const QString &path) {
    // logToDesktop("Getting icon for: " + path);
    
    // 确保 COM 初始化
    CoInitialize(NULL);
    // logToDesktop(QString("CoInitialize result: %1").arg((unsigned long)hr, 0, 16));
    
    SHFILEINFO shfi;
    memset(&shfi, 0, sizeof(shfi));
    QString nativePath = QDir::toNativeSeparators(path);
    QPixmap result;

    // 尝试1: 标准 SHGetFileInfo
    DWORD_PTR res = SHGetFileInfo((const wchar_t*)nativePath.utf16(), 0, &shfi, sizeof(shfi), 
                      SHGFI_ICON | SHGFI_LARGEICON);
    
    if (res != 0) {
        QPixmap pixmap = QtWin::fromHICON(shfi.hIcon);
        DestroyIcon(shfi.hIcon);
        if (!pixmap.isNull()) {
             result = pixmap.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
             // logToDesktop("Success with SHGetFileInfo");
        }
    } else {
        logToDesktop("SHGetFileInfo failed for: " + nativePath);
    }
    
    // 尝试2: 通过文件属性
    if (result.isNull()) {
        memset(&shfi, 0, sizeof(shfi));
        if (SHGetFileInfo((const wchar_t*)nativePath.utf16(), FILE_ATTRIBUTE_NORMAL, &shfi, sizeof(shfi), 
                          SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES)) {
            QPixmap pixmap = QtWin::fromHICON(shfi.hIcon);
            DestroyIcon(shfi.hIcon);
            if (!pixmap.isNull()) {
                 result = pixmap.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                 logToDesktop("Success with SHGetFileInfo (Attributes)");
            }
        }
    }
    
    // 尝试3: ExtractIconEx
    if (result.isNull() && (path.endsWith(".exe", Qt::CaseInsensitive) || path.endsWith(".dll", Qt::CaseInsensitive))) {
        HICON hIconLarge = NULL;
        UINT extracted = ExtractIconEx((const wchar_t*)nativePath.utf16(), 0, &hIconLarge, NULL, 1);
        if (extracted > 0 && hIconLarge) {
            QPixmap pixmap = QtWin::fromHICON(hIconLarge);
            DestroyIcon(hIconLarge);
            if (!pixmap.isNull()) {
                 result = pixmap.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                 logToDesktop("Success with ExtractIconEx");
            }
        } else {
            logToDesktop("ExtractIconEx failed or 0 icons");
        }
    }

    CoUninitialize();
    
    if (result.isNull()) {
        logToDesktop("ALL FAILED for: " + path);
    }
    
    return result;
}

FenceWindow::FenceWindow(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_title(title)
{
    // 初始化保存定时器 (防抖动) - 必须在 setupUi 之前，因为 setupUi 会触发 resizeEvent
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(1000); // 1秒后保存
    connect(m_saveTimer, &QTimer::timeout, this, &FenceWindow::geometryChanged);

    setupUi();
}

FenceWindow::~FenceWindow()
{
}

void FenceWindow::setupUi()
{
    // 设置窗口属性：无边框、置于桌面层、工具窗口（不在任务栏显示）
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnBottomHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    
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
    mainLayout->addWidget(m_titleLabel);

    // 内容区域
    m_contentArea = new QWidget(this);
    m_contentArea->setObjectName("contentArea");
    m_contentLayout = new FlowLayout(m_contentArea, 8, 6, 6);  // 流式布局：边距8，水平间距6，垂直间距6
    mainLayout->addWidget(m_contentArea, 1);

    // 折叠动画将在 setCollapsed 中按需创建
    
    // 启用鼠标追踪并安装事件过滤器，以便在子控件上悬浮时也能更新光标
    m_titleLabel->setMouseTracking(true);
    m_titleLabel->installEventFilter(this);
    m_contentArea->setMouseTracking(true);
    m_contentArea->installEventFilter(this);
}

void FenceWindow::setupBlurEffect()
{
    // Windows 11: 使用原生 Acrylic 和圆角，效果最佳
    if (BlurHelper::isWindows11()) {
         BlurHelper::enableBlur(this, QColor(30, 30, 35, 160), BlurHelper::Acrylic);
         BlurHelper::enableRoundedCorners(this, 10);
    } else {
        // Windows 10: 禁用 DWM 模糊和 Region 裁剪
        // 使用纯 Qt 半透明背景，以获得平滑的抗锯齿圆角，避免黑边和裁剪伪影
        // 不需要调用 enableBlur 或 enableRoundedCorners
    }
}

void FenceWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setupBlurEffect();
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
            // 使用 resize 直接改变大小，因为我们已经解除了限制
            resize(width(), value.toInt());
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
    
    fence->setGeometry(x, y, w, h);
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
             
             // 其他区域视为标题栏
             *result = HTCAPTION;
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
        if (titleBarRect().contains(lp)) {
            *result = HTCAPTION;
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
    
    // 背景色
    // Win11 开启了毛玻璃，可以使用较低透明度
    // Win10 纯色背景，需要较高不透明度以保证可读性
    int alpha = BlurHelper::isWindows11() ? 15 : 200;
    p.fillPath(path, QColor(30, 30, 35, alpha));
    
    // 边框
    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.drawPath(path);

    // 标题栏分隔线
    if (!m_collapsed) {
        p.setPen(QPen(QColor(255, 255, 255, 20), 1));
        p.drawLine(10, 32, width() - 10, 32);
    }
}

QRect FenceWindow::titleBarRect() const
{
    return QRect(0, 0, width(), 32);
}

// 移除 hitTest
// 移除 ResizeEdge 枚举定义（如果在 cpp 中有的话，但它在头文件中定义，cpp 只用了它）

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
            m_isDragging = true;
        }
        
        raise();
        event->accept();
    }
}

void FenceWindow::mouseMoveEvent(QMouseEvent *event)
{
    // 如果正在拖拽
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->globalPos() - m_dragStartGlobalPos;
        move(pos() + delta);
        m_dragStartGlobalPos = event->globalPos();
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

void FenceWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);

    menu.setStyleSheet(R"(
        QMenu {
            background-color: rgba(45, 45, 50, 230);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 8px;
            padding: 4px;
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
        }
        QMenu::item {
            background: transparent;
            color: #ffffff;
            padding: 5px 24px;
            min-height: 20px;
            border-radius: 4px;
            margin: 2px 4px;
        }
        QMenu::item:selected {
            background-color: #505055;
        }
        QMenu::separator {
            height: 1px;
            background: rgba(255, 255, 255, 0.1);
            margin: 4px 8px;
        }
    )");

    QAction *renameAction = menu.addAction("重命名");
    QAction *collapseAction = menu.addAction(m_collapsed ? "展开" : "折叠");
    menu.addSeparator();
    QAction *deleteAction = menu.addAction("删除围栏");

    QAction *selected = menu.exec(event->globalPos());

    if (selected == renameAction) {
        startTitleEdit();
    } else if (selected == collapseAction) {
        setCollapsed(!m_collapsed);
    } else if (selected == deleteAction) {
        emit deleteRequested(this);
    }
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
    }
}

#include <QtWin>

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
        
        // 首先检查图标是否已存在于当前围栏
        for (IconWidget *icon : m_icons) {
            if (icon->path() == iconPath) {
                // 图标已在当前围栏，不需要操作
                event->acceptProposedAction();
                m_hovered = false;
                update();
                return;
            }
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
    m_titleEdit->setFocus();
    m_titleEdit->raise();
    
    // 按 Enter 完成编辑
    connect(m_titleEdit, &QLineEdit::returnPressed, this, &FenceWindow::finishTitleEdit);
    
    // 安装事件过滤器处理 ESC 键和焦点丢失
    m_titleEdit->installEventFilter(this);
}

void FenceWindow::finishTitleEdit()
{
    if (!m_titleEdit) return;
    
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
}

bool FenceWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseMove) {
        // 如果正在调整大小或拖拽，不需要额外处理光标，交由 mouseMoveEvent 处理
        if (!m_isResizing && !m_isDragging) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            QWidget *widget = qobject_cast<QWidget*>(watched);
            if (widget) {
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
                m_titleEdit->removeEventFilter(this);
                m_titleEdit->deleteLater();
                m_titleEdit = nullptr;
                m_titleLabel->setText(m_title);  // 恢复原标题
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

