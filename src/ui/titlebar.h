#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>

/**
 * @brief 自定义无边框标题栏
 * 支持拖拽移动窗口、最小化、最大化、关闭
 */
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);
    ~TitleBar() = default;

    void setTitle(const QString &title);
    void setIcon(const QPixmap &icon);

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void setupUi();
    void updateMaximizeButton();

    QLabel *m_iconLabel;
    QLabel *m_titleLabel;
    QPushButton *m_btnMinimize;
    QPushButton *m_btnMaximize;
    QPushButton *m_btnClose;

    QPoint m_dragPos;
    bool m_isDragging = false;
};

#endif // TITLEBAR_H
