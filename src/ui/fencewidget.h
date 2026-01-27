#ifndef FENCEWIDGET_H
#define FENCEWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QPropertyAnimation>

class IconWidget;

/**
 * @brief 围栏组件
 * 可拖拽调整位置和大小的半透明容器，用于分组管理桌面图标
 */
class FenceWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool collapsed READ isCollapsed WRITE setCollapsed NOTIFY collapsedChanged)

public:
    explicit FenceWidget(const QString &title = "新建围栏", QWidget *parent = nullptr);
    ~FenceWidget() = default;

    QString title() const;
    void setTitle(const QString &title);

    bool isCollapsed() const;
    void setCollapsed(bool collapsed);

    void addIcon(IconWidget *icon);
    void removeIcon(IconWidget *icon);

    QList<IconWidget*> icons() const;

signals:
    void collapsedChanged(bool collapsed);
    void titleChanged(const QString &title);
    void iconDropped(IconWidget *icon);
    void deleteRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void setupUi();
    void updateLayout();
    QRect titleBarRect() const;
    QRect resizeHandleRect() const;

    enum ResizeEdge {
        None = 0,
        Left = 1,
        Right = 2,
        Top = 4,
        Bottom = 8
    };
    ResizeEdge hitTest(const QPoint &pos) const;

    QLabel *m_titleLabel;
    QWidget *m_contentArea;
    QVBoxLayout *m_contentLayout;
    QList<IconWidget*> m_icons;

    QString m_title;
    bool m_collapsed = false;
    int m_expandedHeight = 200;

    // 拖拽状态
    bool m_isDragging = false;
    bool m_isResizing = false;
    QPoint m_dragStartPos;
    QPoint m_dragStartGlobalPos;
    QSize m_resizeStartSize;
    ResizeEdge m_resizeEdge = None;

    // 视觉效果
    bool m_hovered = false;
    QPropertyAnimation *m_collapseAnimation;
};

#endif // FENCEWIDGET_H
