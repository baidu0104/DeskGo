#include "iconwidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QStyle>
#include <QDebug>
#include <QTimer>
#include "../platform/blurhelper.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

IconWidget::IconWidget(const IconData &data, QWidget *parent)
    : QWidget(parent)
    , m_data(data)
{
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
    
    if (!data.icon.isNull()) {
        qreal dpr = devicePixelRatio();
        int size = 48 * dpr;
        QPixmap scaled = data.icon.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);
        m_iconLabel->setPixmap(scaled);
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
    }
    QWidget::mouseMoveEvent(event);
}

void IconWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        // 如果是点击操作（不是拖拽），且在控件范围内
        if (rect().contains(event->pos())) {
            // 打开文件/快捷方式
            if (!m_data.path.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(m_data.path));
                
                // 打开文件后，立即重置父窗口的 Z-order
#ifdef Q_OS_WIN
                QWidget *parentWidget = window();
                if (parentWidget) {
                    QTimer::singleShot(10, parentWidget, [parentWidget]() {
                        HWND hWnd = (HWND)parentWidget->winId();
                        
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
                            
                            qDebug() << "[IconWidget] Z-order reset after opening file";
                        }
                    });
                }
#endif
            }
            emit doubleClicked(); // 仍然发出这个信号以防外部使用，或者可以改名为 clicked
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
    update();
    QWidget::enterEvent(event);
}

void IconWidget::leaveEvent(QEvent *event)
{
    m_hovered = false;
    m_pressed = false;
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
    
    menu.setStyleSheet(R"(
        QMenu {
            background-color: rgba(45, 45, 50, 240);
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

    QAction *deleteAction = menu.addAction("删除");
    QAction *selected = menu.exec(event->globalPos());

    if (selected == deleteAction) {
        emit removeRequested();
    }
}
