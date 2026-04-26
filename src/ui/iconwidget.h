#ifndef ICONWIDGET_H
#define ICONWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

/**
 * @brief 图标组件
 * 显示桌面图标的缩略图和名称
 */
class IconWidget : public QWidget
{
    Q_OBJECT

public:
    struct IconData {
        QString name;        // 显示名称
        QString path;        // 快捷方式/文件路径
        QString targetPath;  // 目标路径
        QString originalSourcePath; // 原始来源路径（用户桌面/公用桌面）
        QPixmap icon;        // 图标
        QPoint originalPosition = QPoint(-1, -1); // 原始桌面坐标
        bool isFromDesktop = false; // 是否来自桌面
        bool alwaysRunAsAdmin = false; // 是否默认以管理员身份启动
    };

    explicit IconWidget(const IconData &data, QWidget *parent = nullptr);
    ~IconWidget() = default;

    IconData data() const;
    void setData(const IconData &data);

    void setTextVisible(bool visible);
    bool isTextVisible() const;

    QString name() const;
    QString path() const;

signals:
    void doubleClicked();
    void dragStarted();
    void removeRequested();
    void launchPreferenceChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void setupUi();
    bool openPath(bool runAsAdmin);
    void resetParentWindowZOrder();

    QLabel *m_iconLabel;
    QLabel *m_nameLabel;
    IconData m_data;

    bool m_hovered = false;
    bool m_pressed = false;
    QPoint m_pressPos;

    QTimer *m_tooltipTimer;
};

#endif // ICONWIDGET_H
