#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QJsonObject>
#include <QSettings>
#include <QRect>
#include <QTimer>

/**
 * @brief 配置管理器
 * 管理应用设置和围栏布局数据的持久化
 */
class ConfigManager : public QObject
{
    Q_OBJECT

public:
    static ConfigManager* instance();

    // 通用设置
    bool autoStart() const;
    void setAutoStart(bool enabled);

    bool minimizeToTray() const;
    void setMinimizeToTray(bool enabled);

    QString theme() const;
    void setTheme(const QString &theme);

    // 图标显示设置
    bool iconTextVisible() const;
    void setIconTextVisible(bool visible);

    // 窗口状态
    QRect windowGeometry() const;
    void setWindowGeometry(const QRect &geometry);

    bool windowMaximized() const;
    void setWindowMaximized(bool maximized);

    // 围栏数据
    QJsonObject fencesData() const;
    void setFencesData(const QJsonObject &data);

    // 存储路径：fences_storage 与配置文件在同目录下
    QString fencesStoragePath() const { return m_fencesStoragePath; }

    // 真正的保存（防抖调用此方法）
    void doSave();
    
    // 强制立即同步保存所有数据（建议在程序退出前调用）
    void sync();
    
    // 阻止任何未完成或将来的写盘操作，丢弃所有更改
    void stopSave();

    // 加载/请求防抖保存
    void requestSave();
    void load();

signals:
    void autoStartChanged(bool enabled);
    void themeChanged(const QString &theme);
    void iconTextVisibleChanged(bool visible);

private:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager();

    void updateAutoStartRegistry(bool enabled);

    QSettings *m_settings;
    QTimer *m_saveDebounceTimer;
    QString m_settingsPath;
    QString m_fencesPath;
    QString m_fencesStoragePath;
    
    bool m_saveDisabled = false;
    bool m_autoStart = false;
    bool m_minimizeToTray = true;
    QString m_theme = "dark";
    bool m_iconTextVisible = true;
    QRect m_windowGeometry;
    bool m_windowMaximized = false;
    QJsonObject m_fencesData;
};

#endif // CONFIGMANAGER_H
