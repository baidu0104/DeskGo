#ifndef FENCEWINDOW_H
#define FENCEWINDOW_H

#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QUuid>
#include <QTimer>
#include <QPointer>
#include <QMoveEvent>
#include <QResizeEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
    
    // 标记窗口已经嵌入桌面
    void setDesktopEmbedded(bool embedded) { m_desktopEmbedded = embedded; }
    
    // 设置用户主动隐藏标志
    void setUserHidden(bool hidden) { m_userHidden = hidden; }
    
    // 设置正在调整 Z-order 标志
    void setAdjustingZOrder(bool adjusting) { m_isAdjustingZOrder = adjusting; }
    
    // 设置始终置顶模式
    void setAlwaysOnTop(bool onTop);
    bool isAlwaysOnTop() const { return m_alwaysOnTop; }
    
    // 获取所有活动的围栏窗口
    static const QSet<FenceWindow*>& allFences() { return s_allFences; }

    // 设置图标文字显示
    void setIconTextVisible(bool visible);

    // 背景颜色设置
    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor &color);
    
    // 立即保存待处理的更改（停止定时器并触发保存）
    void flushPendingSave();

    bool isRestoringFromJson() const { return m_restoringFromJson; }
    
    // 仅停止保存定时器，不触发任何保存信号（用于还原场景，防止覆盖已还原数据）
    void stopSaveTimer();


    // 序列化
    QJsonObject toJson() const;
    static FenceWindow* fromJson(const QJsonObject &json);

signals:
    void collapsedChanged(bool collapsed);
    void titleChanged(const QString &title);
    void deleteRequested(FenceWindow *fence);
    void geometryChanged();
    void firstShowCompleted(); // 首次显示完成信号

public slots:
    void finishTitleEdit();

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
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    void setupBlurEffect();
    void clearDropIndicator();
    void insertIconAt(IconWidget *icon, int index);
    QRect titleBarRect() const;
    void updateBottomAlignmentGuide(const QRect &targetGeo);
    void showBottomAlignmentGuideAt(int localY);
    void clearBottomAlignmentGuide();
    static void clearBottomAlignmentGuides();
    
    // 边缘吸附功能
    static const int SNAP_THRESHOLD = 10; // 吸附阈值（像素）
    QPoint snapPositionToOtherFences(const QPoint& targetPos, const QSize& targetSize) const;
    QRect snapGeometryToOtherFences(const QRect& targetGeo, int resizeEdge) const;
    // 缓存其他围栏尺寸以防止高频对象访问
    QList<QRect> m_snapRectsCache;

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
    int m_nativeHitResizeEdge = None;
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
    
    // 桌面嵌入状态
    bool m_desktopEmbedded = false;
    
    // 键盘钩子句柄
    static HHOOK s_hKeyboardHook;
    static QSet<FenceWindow*> s_allFences; // 所有围栏窗口的集合
    
    // 鼠标钩子（用于标题编辑时检测外部点击）
    static HHOOK s_hMouseHook;
    static QPointer<FenceWindow> s_editingFence; // 使用 QPointer 自动处理对象删除
    
    
    bool m_userHidden = false; // 用户主动隐藏
    bool m_isClosing = false; // 程序正在关闭
    bool m_restoringFromJson = false; // 正在从配置恢复，避免启动阶段误触发保存
    bool m_alwaysOnTop = false; // 始终置顶模式（默认关闭，不遮挡其他窗口）
    bool m_isAdjustingZOrder = false; // 正在调整 Z-order，避免触发 nativeEvent 的干扰
    
    // 键盘钩子回调函数
    static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    
    // 鼠标钩子回调函数
    static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    
    // 拖拽插入位置指示器
    bool m_showDropIndicator = false;
    int m_dropIndicatorIndex = -1;
    QRect m_dropIndicatorRect;

    // 底边缩放对齐线
    QWidget *m_bottomAlignmentGuide = nullptr;
    QString m_alignmentGuideDebugState;

    // 视觉样式
    QColor m_backgroundColor = QColor(30, 30, 35, 200);

    // 标题编辑
    // 状态保存
    QTimer *m_saveTimer;
    void startTitleEdit();
    
    // 引导提示
    QLabel *m_placeholderLabel = nullptr;
    void updatePlaceholder();
};

#endif // FENCEWINDOW_H
