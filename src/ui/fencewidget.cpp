#include "fencewidget.h"
#include "iconwidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QMenu>
#include <QInputDialog>
#include <QMimeData>
#include <QDrag>
#include "../platform/blurhelper.h"

FenceWidget::FenceWidget(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
{
    setupUi();
    setAcceptDrops(true);
    setMouseTracking(true);
}

void FenceWidget::setupUi()
{
    setMinimumSize(180, 60);
    resize(280, 200);

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
            font-size: 13px;
            font-weight: 500;
            background: transparent;
            padding: 0 12px;
        }
    )");
    mainLayout->addWidget(m_titleLabel);

    // 内容区域
    m_contentArea = new QWidget(this);
    m_contentArea->setObjectName("contentArea");
    m_contentLayout = new QVBoxLayout(m_contentArea);
    m_contentLayout->setContentsMargins(8, 8, 8, 8);
    m_contentLayout->setSpacing(8);
    m_contentLayout->addStretch();
    mainLayout->addWidget(m_contentArea, 1);

    // 折叠动画
    m_collapseAnimation = new QPropertyAnimation(this, "minimumHeight", this);
    m_collapseAnimation->setDuration(200);
    m_collapseAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

QString FenceWidget::title() const
{
    return m_title;
}

void FenceWidget::setTitle(const QString &title)
{
    if (m_title != title) {
        m_title = title;
        m_titleLabel->setText(title);
        emit titleChanged(title);
    }
}

bool FenceWidget::isCollapsed() const
{
    return m_collapsed;
}

void FenceWidget::setCollapsed(bool collapsed)
{
    if (m_collapsed != collapsed) {
        m_collapsed = collapsed;

        if (collapsed) {
            m_expandedHeight = height();
            m_collapseAnimation->setStartValue(height());
            m_collapseAnimation->setEndValue(32);
        } else {
            m_collapseAnimation->setStartValue(height());
            m_collapseAnimation->setEndValue(m_expandedHeight);
        }

        m_contentArea->setVisible(!collapsed);
        m_collapseAnimation->start();

        emit collapsedChanged(collapsed);
    }
}

void FenceWidget::addIcon(IconWidget *icon)
{
    if (icon && !m_icons.contains(icon)) {
        m_icons.append(icon);
        icon->setParent(m_contentArea);
        m_contentLayout->insertWidget(m_contentLayout->count() - 1, icon);
        icon->show();
    }
}

void FenceWidget::removeIcon(IconWidget *icon)
{
    if (icon && m_icons.contains(icon)) {
        m_icons.removeOne(icon);
        m_contentLayout->removeWidget(icon);
    }
}

QList<IconWidget*> FenceWidget::icons() const
{
    return m_icons;
}

void FenceWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 绘制圆角矩形背景
    QPainterPath path;
    path.addRoundedRect(rect(), 12, 12);

    // 背景色（调整为与 FenceWindow 一致的深色系，并降低不透明度以透出背景模糊）
    // 平常: (30, 30, 35, 80), 悬停: (60, 60, 65, 120)
    QColor bgColor = m_hovered ? QColor(60, 60, 65, 120) : QColor(30, 30, 35, 80);
    painter.fillPath(path, bgColor);

    // 边框
    painter.setPen(QPen(QColor(255, 255, 255, 40), 1));
    painter.drawPath(path);

    // 标题栏分隔线
    if (!m_collapsed) {
        painter.setPen(QPen(QColor(255, 255, 255, 30), 1));
        painter.drawLine(12, 32, width() - 12, 32);
    }
}

QRect FenceWidget::titleBarRect() const
{
    return QRect(0, 0, width(), 32);
}

FenceWidget::ResizeEdge FenceWidget::hitTest(const QPoint &pos) const
{
    const int margin = 8;
    ResizeEdge edge = None;

    if (pos.x() < margin) edge = static_cast<ResizeEdge>(edge | Left);
    if (pos.x() > width() - margin) edge = static_cast<ResizeEdge>(edge | Right);
    if (pos.y() < margin) edge = static_cast<ResizeEdge>(edge | Top);
    if (pos.y() > height() - margin) edge = static_cast<ResizeEdge>(edge | Bottom);

    return edge;
}

void FenceWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
        m_dragStartGlobalPos = event->globalPos();
        m_resizeStartSize = size();

        m_resizeEdge = hitTest(event->pos());
        if (m_resizeEdge != None && !m_collapsed) {
            m_isResizing = true;
        } else if (titleBarRect().contains(event->pos())) {
            m_isDragging = true;
        }

        raise();  // 置顶
        event->accept();
    }
}

void FenceWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        QPoint delta = event->globalPos() - m_dragStartGlobalPos;
        move(pos() + delta);
        m_dragStartGlobalPos = event->globalPos();
    } else if (m_isResizing) {
        QPoint delta = event->globalPos() - m_dragStartGlobalPos;
        QRect geo = geometry();

        if (m_resizeEdge & Left) {
            int newWidth = m_resizeStartSize.width() - delta.x();
            if (newWidth >= minimumWidth()) {
                geo.setLeft(geo.right() - newWidth);
            }
        }
        if (m_resizeEdge & Right) {
            geo.setWidth(qMax(minimumWidth(), m_resizeStartSize.width() + delta.x()));
        }
        if (m_resizeEdge & Top) {
            int newHeight = m_resizeStartSize.height() - delta.y();
            if (newHeight >= minimumHeight()) {
                geo.setTop(geo.bottom() - newHeight);
            }
        }
        if (m_resizeEdge & Bottom) {
            geo.setHeight(qMax(minimumHeight(), m_resizeStartSize.height() + delta.y()));
        }

        setGeometry(geo);
    } else {
        // 更新鼠标光标
        ResizeEdge edge = hitTest(event->pos());
        if (!m_collapsed && edge != None) {
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
            setCursor(Qt::ArrowCursor);
        }
    }
    event->accept();
}

void FenceWidget::mouseReleaseEvent(QMouseEvent *event)
{
    m_isDragging = false;
    m_isResizing = false;
    event->accept();
}

void FenceWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && titleBarRect().contains(event->pos())) {
        setCollapsed(!m_collapsed);
    }
}

void FenceWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_collapsed) {
        m_expandedHeight = height();
    }
}

void FenceWidget::enterEvent(QEvent *event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void FenceWidget::leaveEvent(QEvent *event)
{
    m_hovered = false;
    setCursor(Qt::ArrowCursor);
    update();
    QWidget::leaveEvent(event);
}

void FenceWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-deskgo-icon")) {
        event->acceptProposedAction();
    }
}

void FenceWidget::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-deskgo-icon")) {
        QString iconPath = QString::fromUtf8(event->mimeData()->data("application/x-deskgo-icon"));
        
        // 在父窗口的所有围栏中查找该图标
        IconWidget *iconWidget = nullptr;
        FenceWidget *sourceFence = nullptr;
        
        // 获取父窗口，然后查找所有围栏
        if (parentWidget()) {
            QList<FenceWidget*> allFences = parentWidget()->findChildren<FenceWidget*>();
            for (FenceWidget *fence : allFences) {
                for (IconWidget *icon : fence->icons()) {
                    if (icon->path() == iconPath) {
                        iconWidget = icon;
                        sourceFence = fence;
                        break;
                    }
                }
                if (iconWidget) break;
            }
        }
        
        // 如果找到图标且不是来自同一个围栏，则移动它
        if (iconWidget && sourceFence && sourceFence != this) {
            sourceFence->removeIcon(iconWidget);
            addIcon(iconWidget);
            emit iconDropped(iconWidget);
        }
        
        event->acceptProposedAction();
    }
}

void FenceWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    
    // 启用毛玻璃效果
    BlurHelper::enableBlur(&menu, QColor(30, 30, 35, 160), BlurHelper::Acrylic);
    BlurHelper::enableRoundedCorners(&menu);

    menu.setStyleSheet(R"(
        QMenu {
            background: rgba(30, 30, 35, 10);
            border: 1px solid rgba(255, 255, 255, 0.08);
            border-radius: 8px;
            padding: 4px;
        }
        QMenu::item {
            color: #ffffff;
            padding: 8px 24px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background: rgba(255, 255, 255, 0.1);
        }
    )");

    QAction *renameAction = menu.addAction("重命名");
    QAction *collapseAction = menu.addAction(m_collapsed ? "展开" : "折叠");
    menu.addSeparator();
    QAction *deleteAction = menu.addAction("删除围栏");

    QAction *selected = menu.exec(event->globalPos());

    if (selected == renameAction) {
        bool ok;
        QString newTitle = QInputDialog::getText(this, "重命名围栏", "名称:", 
                                                  QLineEdit::Normal, m_title, &ok);
        if (ok && !newTitle.isEmpty()) {
            setTitle(newTitle);
        }
    } else if (selected == collapseAction) {
        setCollapsed(!m_collapsed);
    } else if (selected == deleteAction) {
        emit deleteRequested();
    }
}

void FenceWidget::updateLayout()
{
    // 重新排列图标
    // TODO: 实现网格布局
}
