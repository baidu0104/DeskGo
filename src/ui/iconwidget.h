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
        QPixmap icon;        // 图标
        QPoint originalPosition = QPoint(-1, -1); // 原始桌面坐标
        bool isFromDesktop = false; // 是否来自桌面
    };

    explicit IconWidget(const IconData &data, QWidget *parent = nullptr);
    ~IconWidget() = default;

    IconData data() const;
    void setData(const IconData &data);

    QString name() const;
    QString path() const;

signals:
    void doubleClicked();
    void dragStarted();
    void removeRequested();

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

    QLabel *m_iconLabel;
    QLabel *m_nameLabel;
    IconData m_data;

    bool m_hovered = false;
    bool m_pressed = false;
    QPoint m_pressPos;
};

#endif // ICONWIDGET_H
