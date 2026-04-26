#ifndef STYLEHELPER_H
#define STYLEHELPER_H

#include <QString>

/**
 * @brief 样式助手类
 * 集中管理应用中硬编码的 QSS 样式表，提高可维护性。
 */
class StyleHelper {
public:
    // 托盘菜单统一样式
    static inline QString trayMenuStyle() {
        return R"(
            QMenu {
                background-color: rgba(45, 45, 50, 240);
                border: 1px solid rgba(255, 255, 255, 0.1);
                border-radius: 12px;
                padding: 8px;
            }
            QMenu::item {
                background: transparent;
                color: #ffffff;
                padding: 8px 24px;
                border-radius: 6px;
                margin: 2px 4px;
                font-family: "Microsoft YaHei", "Segoe UI";
                font-size: 13px;
            }
            QMenu::item:selected {
                background-color: rgba(255, 255, 255, 0.1);
            }
            QMenu::separator {
                height: 1px;
                background: rgba(255, 255, 255, 0.1);
                margin: 6px 12px;
            }
            QLabel#actionLabel {
                color: #ffffff;
                font-family: "Microsoft YaHei", "Segoe UI";
                font-size: 13px;
                border-radius: 6px;
                padding: 8px 0px;
                background: transparent;
            }
            QLabel#actionLabel:hover {
                background-color: rgba(255, 255, 255, 0.1);
            }
        )";
    }

    // 子菜单样式（减小 padding）
    static inline QString subMenuStyle() {
        return trayMenuStyle() + R"(
            QMenu::item {
                padding: 8px 12px;
            }
        )";
    }

    // 围栏标题栏样式
    static inline QString fenceTitleStyle() {
        return R"(
            QLabel {
                color: #ffffff;
                font-size: 12px;
                font-weight: 500;
                background: transparent;
                padding: 0 10px;
            }
        )";
    }

    // 占位提示文字样式
    static inline QString placeholderStyle() {
        return R"(
            QLabel {
                color: rgba(255, 255, 255, 0.7);
                font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
                font-size: 15px;
                font-weight: 400;
                letter-spacing: 2px;
                background: transparent;
            }
        )";
    }

    // 标题编辑框样式
    static inline QString titleEditStyle() {
        return R"(
            QLineEdit {
                color: #ffffff;
                font-size: 12px;
                font-weight: 500;
                background: transparent;
                border: none;
                border-bottom: 2px solid rgba(100, 150, 255, 0.8);
                padding: 0 10px;
            }
        )";
    }

    // 右键上下文菜单样式
    static inline QString contextMenuStyle() {
        return R"(
            QMenu {
                background-color: rgba(45, 45, 50, 240);
                border: 1px solid rgba(255, 255, 255, 0.1);
                border-radius: 12px;
                padding: 8px;
                font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
                font-size: 13px;
                icon-size: 14px;
            }
            QMenu::item {
                background: transparent;
                color: #ffffff;
                padding: 4px 36px 4px 4px; 
                min-height: 22px;
                border: 1px solid transparent;
                border-radius: 6px;
                margin: 1px 4px;
            }
            QMenu::item:selected {
                background-color: rgba(255, 255, 255, 0.1);
                border: 1px solid rgba(255, 255, 255, 0.15);
            }
            QMenu::right-arrow {
                width: 12px;
                height: 12px;
                right: 8px;
            }
            QMenu::indicator:checked {
                position: absolute;
                left: 1px;
                width: 10px;
                height: 10px;
                image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSJ3aGl0ZSIgc3Ryb2tlLXdpZHRoPSIzLjgiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIgc3Ryb2tlLWxpbmVqb2luPSJyb3VuZCI+PHBhdGggZD0iTTIwIDZMOSAxN0w0IDEyIiAvPjwvc3ZnPg==);
            }
            QMenu::separator {
                height: 1px;
                background: rgba(255, 255, 255, 0.15);
                margin: 4px 8px;
            }
        )";
    }
};

#endif // STYLEHELPER_H
