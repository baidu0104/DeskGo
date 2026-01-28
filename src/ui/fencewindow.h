#ifndef FENCEWINDOW_H
#define FENCEWINDOW_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QUuid>
#include <QTimer>
#include <QMoveEvent>
#include <QResizeEvent>

class IconWidget;

/**
 * @brief 桌面围栏窗口
 * 直接显示在桌面上的独立毛玻璃窗口
 */
class FenceWindow : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool collapsed READ isCollapsed WRITE setCollapsed NOTIFY collapsedChanged)

public:
    explicit FenceWindow(const QString &title = "新建围栏", QWidget *parent = nullptr);
    ~FenceWindow();

    QString id() const { return m_id; }
    void setId(const QString &id) { m_id = id; }

    QString title() const;
    void setTitle(const QString &title);

    bool isCollapsed() const;
    void setCollapsed(bool collapsed);

    void addIcon(IconWidget *icon);
    void removeIcon(IconWidget *icon);
    void restoreAllIcons();
    QList<IconWidget*> icons() const;

    // 序列化
    QJsonObject toJson() const;
    static FenceWindow* fromJson(const QJsonObject &json);

signals:
    void collapsedChanged(bool collapsed);
    void titleChanged(const QString &title);
    void deleteRequested(FenceWindow *fence);
    void geometryChanged();

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override; // Added this line
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    void setupBlurEffect();
    QRect titleBarRect() const;

    QString m_id;
    QLabel *m_titleLabel;
    QLineEdit *m_titleEdit = nullptr;  // 标题编辑框
    QWidget *m_contentArea;
    QLayout *m_contentLayout;
    QList<IconWidget*> m_icons;

    QString m_title;
    bool m_collapsed = false;
    int m_expandedHeight = 200;

    // 调整大小状态
    enum ResizeEdge {
        None = 0,
        Left = 1,
        Top = 2,
        Right = 4,
        Bottom = 8
    };
    int m_resizeEdge = None;
    bool m_isResizing = false;
    QSize m_resizeStartSize;
    QRect m_resizeStartGeo;

    // 拖拽状态
    bool m_isDragging = false;
    QPoint m_dragStartPos;
    QPoint m_dragStartGlobalPos;


    // 视觉效果
    bool m_hovered = false;
    QVariantAnimation *m_collapseAnimation = nullptr;
    
    // 拖拽插入位置指示器
    bool m_showDropIndicator = false;
    int m_dropIndicatorIndex = -1;
    QRect m_dropIndicatorRect;
    
    // 标题编辑
    // 状态保存
    QTimer *m_saveTimer;

    void startTitleEdit();
    void finishTitleEdit();
};

#endif // FENCEWINDOW_H
