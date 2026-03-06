#include "titlebar.h"
#include <QMouseEvent>
#include <QApplication>

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void TitleBar::setupUi()
{
    setFixedHeight(40);
    setObjectName("titleBar");

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 8, 0);
    layout->setSpacing(8);

    // 图标
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(20, 20);
    m_iconLabel->setScaledContents(true);
    layout->addWidget(m_iconLabel);

    // 标题
    m_titleLabel = new QLabel("DeskGo", this);
    m_titleLabel->setObjectName("titleLabel");
    layout->addWidget(m_titleLabel);

    layout->addStretch();

    // 最小化按钮
    m_btnMinimize = new QPushButton(this);
    m_btnMinimize->setObjectName("btnMinimize");
    m_btnMinimize->setFixedSize(46, 32);
    m_btnMinimize->setText("─");
    m_btnMinimize->setToolTip("最小化");
    connect(m_btnMinimize, &QPushButton::clicked, this, &TitleBar::minimizeRequested);
    layout->addWidget(m_btnMinimize);

    // 最大化按钮
    m_btnMaximize = new QPushButton(this);
    m_btnMaximize->setObjectName("btnMaximize");
    m_btnMaximize->setFixedSize(46, 32);
    m_btnMaximize->setText("□");
    m_btnMaximize->setToolTip("最大化");
    connect(m_btnMaximize, &QPushButton::clicked, this, &TitleBar::maximizeRequested);
    layout->addWidget(m_btnMaximize);

    // 关闭按钮
    m_btnClose = new QPushButton(this);
    m_btnClose->setObjectName("btnClose");
    m_btnClose->setFixedSize(46, 32);
    m_btnClose->setText("×");
    m_btnClose->setToolTip("关闭");
    connect(m_btnClose, &QPushButton::clicked, this, &TitleBar::closeRequested);
    layout->addWidget(m_btnClose);

    // 样式
    setStyleSheet(R"(
        #titleBar {
            background: transparent;
        }
        #titleLabel {
            color: #ffffff;
            font-size: 14px;
            font-weight: 500;
        }
        QPushButton {
            background: transparent;
            border: none;
            border-radius: 4px;
            color: #ffffff;
            font-size: 16px;
            font-weight: bold;
        }
        QPushButton:hover {
            background: rgba(255, 255, 255, 0.1);
        }
        #btnClose:hover {
            background: #e81123;
        }
    )");
}

void TitleBar::setTitle(const QString &title)
{
    m_titleLabel->setText(title);
}

void TitleBar::setIcon(const QPixmap &icon)
{
    m_iconLabel->setPixmap(icon);
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_dragPos = event->globalPos() - window()->frameGeometry().topLeft();
        event->accept();
    }
}

void TitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        QWidget *win = window();
        if (win->isMaximized()) {
            // 从最大化状态拖出时，恢复窗口
            win->showNormal();
            // 调整拖拽位置
            int newX = event->globalPos().x() - win->width() / 2;
            int newY = event->globalPos().y() - 20;
            win->move(newX, newY);
            m_dragPos = QPoint(win->width() / 2, 20);
        } else {
            win->move(event->globalPos() - m_dragPos);
        }
        event->accept();
    }
}

void TitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_isDragging = false;
    event->accept();
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit maximizeRequested();
    }
}

void TitleBar::updateMaximizeButton()
{
    if (window()->isMaximized()) {
        m_btnMaximize->setText("❐");
        m_btnMaximize->setToolTip("还原");
    } else {
        m_btnMaximize->setText("□");
        m_btnMaximize->setToolTip("最大化");
    }
}
