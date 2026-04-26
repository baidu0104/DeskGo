#include "iconwidget.h"
#include "fencewindow.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QStyle>
#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QToolTip>
#include <QHelpEvent>
#include "../platform/blurhelper.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

IconWidget::IconWidget(const IconData &data, QWidget *parent)
    : QWidget(parent)
    , m_data(data)
{
    m_tooltipTimer = new QTimer(this);
    m_tooltipTimer->setSingleShot(true);
    connect(m_tooltipTimer, &QTimer::timeout, this, [this]() {
        if (m_hovered) {
            QToolTip::showText(QCursor::pos(), m_data.name, this);
        }
    });

    setupUi();
}

void IconWidget::setupUi()
{
    setFixedSize(80, 90);
    setCursor(Qt::PointingHandCursor);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignCenter);

    // 图标
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(48, 48);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setScaledContents(false);
    m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    if (!m_data.icon.isNull()) {
        qreal dpr = devicePixelRatio();
        int size = 48 * dpr;
        QPixmap scaled = m_data.icon.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);
        m_iconLabel->setPixmap(scaled);
    } else {
        // 图标加载失败时的后备显示：使用系统标准文件图标
        QIcon fallbackIcon = style()->standardIcon(QStyle::SP_FileIcon);
        // AA_UseHighDpiPixmaps 启用时，QIcon::pixmap(48, 48) 会自动返回高分屏 Pixmap
        m_iconLabel->setPixmap(fallbackIcon.pixmap(48, 48));
    }
    layout->addWidget(m_iconLabel, 0, Qt::AlignCenter);

    // 名称
    m_nameLabel = new QLabel(this);
    m_nameLabel->setAlignment(Qt::AlignCenter);
    m_nameLabel->setWordWrap(false);  // 不换行
    m_nameLabel->setFixedWidth(72);   // 限制宽度
    m_nameLabel->setMaximumHeight(30);
    m_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    
    // 截断长文本
    QFontMetrics fm(m_nameLabel->font());
    QString elidedText = fm.elidedText(m_data.name, Qt::ElideMiddle, 68);
    m_nameLabel->setText(elidedText);
    
    m_nameLabel->setStyleSheet(R"(
        QLabel {
            color: #ffffff;
            font-size: 12px;
            background: transparent;
        }
    )");
    layout->addWidget(m_nameLabel);

    // 设置 ToolTip
    QString tip = m_data.name;
    setToolTip(tip);
    m_iconLabel->setToolTip(tip);
    m_nameLabel->setToolTip(tip);
}

IconWidget::IconData IconWidget::data() const
{
    return m_data;
}

void IconWidget::setData(const IconData &data)
{
    m_data = data;
    
    // 截断长文本
    QFontMetrics fm(m_nameLabel->font());
    QString elidedText = fm.elidedText(data.name, Qt::ElideMiddle, 68);
    m_nameLabel->setText(elidedText);

    // 设置 ToolTip
    QString tip = data.name;
    setToolTip(tip);
    m_iconLabel->setToolTip(tip);
    m_nameLabel->setToolTip(tip);
    
    if (!data.icon.isNull()) {
        qreal dpr = devicePixelRatio();
        int size = 48 * dpr;
        QPixmap scaled = data.icon.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);
        m_iconLabel->setPixmap(scaled);
    } else {
        // 图标加载失败时的后备显示
        QIcon fallbackIcon = style()->standardIcon(QStyle::SP_FileIcon);
        m_iconLabel->setPixmap(fallbackIcon.pixmap(48, 48));
    }
}

void IconWidget::setTextVisible(bool visible)
{
    m_nameLabel->setVisible(visible);
}

bool IconWidget::isTextVisible() const
{
    return m_nameLabel->isVisible();
}

QString IconWidget::name() const
{
    return m_data.name;
}

QString IconWidget::path() const
{
    return m_data.path;
}

bool IconWidget::openPath(bool runAsAdmin)
{
    if (m_data.path.isEmpty()) {
        return false;
    }

#ifdef Q_OS_WIN
    if (runAsAdmin) {
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(SHELLEXECUTEINFOW);
        sei.fMask = SEE_MASK_NOASYNC;
        sei.hwnd = (HWND)window()->winId();
        sei.lpVerb = L"runas";

        const QString nativePath = QDir::toNativeSeparators(m_data.path);
        const std::wstring wPath = nativePath.toStdWString();
        sei.lpFile = wPath.c_str();
        sei.nShow = SW_SHOWNORMAL;

        if (!ShellExecuteExW(&sei)) {
            const DWORD error = GetLastError();
            qWarning() << "[IconWidget] Failed to launch as admin:" << nativePath
                       << "error:" << error;
            return false;
        }

        resetParentWindowZOrder();
        return true;
    }
#endif

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_data.path))) {
        qWarning() << "[IconWidget] Failed to open path:" << m_data.path;
        return false;
    }

    resetParentWindowZOrder();
    return true;
}

void IconWidget::resetParentWindowZOrder()
{
#ifdef Q_OS_WIN
    QWidget *parentWidget = window();
    if (!parentWidget) {
        return;
    }

    QTimer::singleShot(10, parentWidget, [parentWidget]() {
        HWND hWnd = (HWND)parentWidget->winId();

        HWND hProgman = FindWindow(L"Progman", NULL);
        HWND hDefView = FindWindowEx(hProgman, NULL, L"SHELLDLL_DefView", NULL);

        if (!hDefView) {
            HWND hWorkerW = NULL;
            while ((hWorkerW = FindWindowEx(NULL, hWorkerW, L"WorkerW", NULL)) != NULL) {
                hDefView = FindWindowEx(hWorkerW, NULL, L"SHELLDLL_DefView", NULL);
                if (hDefView) {
                    break;
                }
            }
        }

        HWND hListView = NULL;
        if (hDefView) {
            hListView = FindWindowEx(hDefView, NULL, L"SysListView32", NULL);
        }

        if (hListView) {
            SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetWindowPos(hWnd, hListView, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

            qDebug() << "[IconWidget] Z-order reset after opening file";
        }
    });
#endif
}

void IconWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_hovered || m_pressed) {
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(2, 2, -2, -2), 6, 6);

        QColor bgColor = m_pressed ? QColor(255, 255, 255, 40) : QColor(255, 255, 255, 25);
        painter.fillPath(path, bgColor);

        painter.setPen(QPen(QColor(255, 255, 255, 50), 1));
        painter.drawPath(path);
    }
}

void IconWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_pressPos = event->pos();
        update();
    }
    QWidget::mousePressEvent(event);
}

void IconWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressed && (event->pos() - m_pressPos).manhattanLength() > 10) {
        // 开始拖拽隐藏
        m_tooltipTimer->stop();
        QToolTip::hideText();

        // 开始拖拽
        m_pressed = false;
        update();

        qDebug() << "[IconWidget] Starting drag for:" << m_data.path;

        QDrag *drag = new QDrag(this);
        QMimeData *mimeData = new QMimeData();
        mimeData->setData("application/x-deskgo-icon", m_data.path.toUtf8());
        drag->setMimeData(mimeData);

        if (!m_data.icon.isNull()) {
            qreal dpr = devicePixelRatio();
            int size = 48 * dpr;
            QPixmap scaled = m_data.icon.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            scaled.setDevicePixelRatio(dpr);
            drag->setPixmap(scaled);
        }

        emit dragStarted();
        Qt::DropAction result = drag->exec(Qt::MoveAction);
        qDebug() << "[IconWidget] Drag finished, result:" << result;

        // 增加“拖出恢复”功能：
        // 如果拖拽动作被忽略（result == Qt::IgnoreAction），
        // 且鼠标释放位置在任何围栏窗口之外，则视为用户想要将其拖回桌面。
        if (result == Qt::IgnoreAction) {
            QPoint globalPos = QCursor::pos();
            bool outsideAll = true;
            for (FenceWindow *fence : FenceWindow::allFences()) {
                if (fence && fence->isVisible() && fence->geometry().contains(globalPos)) {
                    outsideAll = false;
                    break;
                }
            }
            if (outsideAll) {
                qDebug() << "[IconWidget] Dragged outside all fences, requesting restoration to desktop.";
                emit removeRequested();
            }
        }
    }
    QWidget::mouseMoveEvent(event);
}

void IconWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        // 如果是点击操作（不是拖拽），且在控件范围内
        if (rect().contains(event->pos())) {
            if (openPath(m_data.alwaysRunAsAdmin)) {
                emit doubleClicked(); // 仍然发出这个信号以防外部使用，或者可以改名为 clicked
            }
        }
    }
    m_pressed = false;
    update();
    QWidget::mouseReleaseEvent(event);
}

void IconWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    // 已经改为单击打开，这里不再处理
    QWidget::mouseDoubleClickEvent(event);
}

void IconWidget::enterEvent(QEvent *event)
{
    m_hovered = true;
    m_tooltipTimer->start(600); // 鼠标悬停600ms后触发提示
    update();
    QWidget::enterEvent(event);
}

void IconWidget::leaveEvent(QEvent *event)
{
    m_hovered = false;
    m_pressed = false;
    m_tooltipTimer->stop();
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(event);
}

void IconWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setAttribute(Qt::WA_NoSystemBackground);
    // 给边框留出 1px 的边距，防止边缘毛刺
    menu.setContentsMargins(1, 1, 1, 1);
    
    menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);

    const QPixmap checkedPixmap = []() {
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#4CAF50"), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(3.0, 8.5), QPointF(6.5, 12.0));
        painter.drawLine(QPointF(6.5, 12.0), QPointF(13.0, 4.0));
        return pixmap;
    }();
    const QPixmap emptyPixmap = []() {
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        return pixmap;
    }();
    
    menu.setStyleSheet(R"(
        QMenu {
            background-color: rgba(45, 45, 50, 240);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 12px;
            padding: 8px;
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
            font-size: 13px;
            icon-size: 14px;
        }
        QMenu::item {
            background: transparent;
            color: #ffffff;
            padding: 4px 36px 4px 4px;
            min-height: 22px;
            border: 1px solid transparent;
            border-radius: 6px;
            margin: 1px 4px;
        }
        QMenu::item:selected {
            background-color: rgba(255, 255, 255, 0.1);
            border: 1px solid rgba(255, 255, 255, 0.15);
        }
        QMenu::right-arrow {
            width: 12px;
            height: 12px;
            right: 8px;
        }
        QMenu::separator {
            height: 1px;
            background: rgba(255, 255, 255, 0.15);
            margin: 4px 8px;
        }
    )");

#ifdef Q_OS_WIN
    connect(&menu, &QMenu::aboutToShow, this, [this, &menu]() {
        QTimer::singleShot(10, this, [this, &menu]() {
            HWND hMenu = (HWND)menu.winId();
            HWND hFence = (HWND)window()->winId();
            
            // 1. 移除 TOPMOST
            LONG_PTR exStyle = GetWindowLongPtr(hMenu, GWL_EXSTYLE);
            if (exStyle & WS_EX_TOPMOST) {
                SetWindowLongPtr(hMenu, GWL_EXSTYLE, exStyle & ~WS_EX_TOPMOST);
            }
            
            // 2. 物理裁剪圆角 (解决黑点问题)
            BlurHelper::enableRoundedCorners(&menu, 8);
            
            // 3. 确保能点击外部关闭
            SetForegroundWindow(hMenu);

            // 4. 调整 Z-order 到围栏上方
            HWND hPrev = GetWindow(hFence, GW_HWNDPREV);
            UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED;
            
            if (hPrev) {
                SetWindowPos(hMenu, hPrev, 0, 0, 0, 0, flags);
            } else {
                SetWindowPos(hMenu, HWND_TOP, 0, 0, 0, 0, flags);
            }
        });
    });
#endif

    QAction *alwaysRunAsAdminAction = menu.addAction("总是以管理员身份运行");
    alwaysRunAsAdminAction->setIcon(QIcon(m_data.alwaysRunAsAdmin ? checkedPixmap : emptyPixmap));
    alwaysRunAsAdminAction->setIconVisibleInMenu(true);

    QAction *runAsAdminAction = menu.addAction("以管理员身份运行");
    runAsAdminAction->setIcon(QIcon(emptyPixmap));
    runAsAdminAction->setIconVisibleInMenu(true);
    menu.addSeparator();
    QAction *deleteAction = menu.addAction("删除");
    deleteAction->setIcon(QIcon(emptyPixmap));
    deleteAction->setIconVisibleInMenu(true);
    QAction *propertiesAction = menu.addAction("属性");
    propertiesAction->setIcon(QIcon(emptyPixmap));
    propertiesAction->setIconVisibleInMenu(true);
    QAction *selected = menu.exec(event->globalPos());

    if (selected == alwaysRunAsAdminAction) {
        const bool newValue = !m_data.alwaysRunAsAdmin;
        if (m_data.alwaysRunAsAdmin != newValue) {
            m_data.alwaysRunAsAdmin = newValue;
            emit launchPreferenceChanged();
        }
    } else if (selected == runAsAdminAction) {
        openPath(true);
    } else if (selected == deleteAction) {
        emit removeRequested();
    } else if (selected == propertiesAction) {
#ifdef Q_OS_WIN
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(SHELLEXECUTEINFOW);
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        sei.hwnd = (HWND)window()->winId();
        sei.lpVerb = L"properties";
        std::wstring wPath = m_data.path.toStdWString();
        sei.lpFile = wPath.c_str();
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
#endif
    }
}
