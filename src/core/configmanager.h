#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QRect>
#include <QSettings>
#include <QTimer>
#include <atomic>

/**
 * @brief 配置管理器
 * 管理应用设置和围栏布局数据的持久化
 */
class ConfigManager : public QObject {
  Q_OBJECT

public:
  enum class LoadResult {
    NotExist,
    IOError,
    ParseError,
    EmptyData,
    Success
  };

  static ConfigManager *instance();

  // 通用设置
  bool autoStart() const;
  void setAutoStart(bool enabled);
  void syncAutoStartWithSystem();

  bool minimizeToTray() const;
  void setMinimizeToTray(bool enabled);

  QString theme() const;
  void setTheme(const QString &theme);

  // 图标显示设置
  bool iconTextVisible() const;
  void setIconTextVisible(bool visible);

  // 布局锁定
  bool layoutLocked() const;
  void setLayoutLocked(bool locked);

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
  bool sync();
  bool forceSync();
  bool waitForIdleAndForceSync();

  // 阻止任何未完成或将来的写盘操作，丢弃所有更改（restore 专用）
  void stopSave();

  // 恢复被 stopSave() 禁止的写盘能力（restore 失败时调用）
  void resumeSave();

  // 日志记录
  static void writeLog(const QString &msg);

  // 加载/请求防抖保存
  void requestSave();
  void load();
  LoadResult lastLoadResult() const;

signals:
  void autoStartChanged(bool enabled);
  void themeChanged(const QString &theme);
  void iconTextVisibleChanged(bool visible);
  void layoutLockedChanged(bool locked);
  void autoStartStatusMessage(const QString &title, const QString &message);

private:
  explicit ConfigManager(QObject *parent = nullptr);
  ~ConfigManager();

  bool updateAutoStartRegistry(bool enabled);
  LoadResult tryLoadJson(const QString &path);
  bool syncInternal(bool ignoreSaveDisabled);

  QSettings *m_settings;
  QTimer *m_saveDebounceTimer;
  QString m_settingsPath;
  QString m_fencesPath;
  QString m_fencesStoragePath;

  bool m_saveDisabled = false;
  std::atomic<bool> m_autoStart{false};
  bool m_minimizeToTray = true;
  QString m_theme = "dark";
  bool m_iconTextVisible = true;
  bool m_layoutLocked = false;
  QRect m_windowGeometry;
  bool m_windowMaximized = false;
  QJsonObject m_fencesData;
  bool m_fencesDirty = false;
  LoadResult m_lastLoadResult = LoadResult::NotExist;
  static QMutex s_logMutex;
  mutable QMutex m_stateMutex;
  QMutex m_syncMutex;
};

#endif // CONFIGMANAGER_H
