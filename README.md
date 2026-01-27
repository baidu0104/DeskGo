# DeskGo

<p align="center">
  <img src="resources/icons/app.png" width="128" height="128" alt="DeskGo Logo">
</p>

<p align="center">
  <strong>一款基于 C++ & Qt 的现代化、轻量级 Windows 桌面围栏管理工具</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Platform-Windows-blue" alt="Platform">
  <img src="https://img.shields.io/badge/Language-C++-red" alt="Language">
  <img src="https://img.shields.io/badge/Framework-Qt%205.15-green" alt="Framework">
  <img src="https://img.shields.io/badge/Style-Glassmorphism-orange" alt="Style">
</p>

---

## 🌟 项目简介

**DeskGo** 旨在解决 Windows 桌面图标杂乱的问题，通过“围栏（Fences）”的概念将桌面划分为不同的功能区域。它不仅提供了高效的收纳能力，更深度契合了 Windows 10/11 的现代美学设计。

## ✨ 核心特性

- 💎 **现代美学设计**：深度适配 Windows DWM 系统，支持 **Acrylic（亚克力）** 和 **毛玻璃（Blur Behind）** 特效，界面通透优雅。
- 📦 **智能围栏管理**：
  - **自由创建**：通过托盘菜单随时新建围栏。
  - **交互灵活**：支持自由拖动位置、无级缩放大小。
  - **一键折叠**：双击标题栏或点击折叠按钮，快速收纳内容，节省空间。
  - **标题编辑**：双击标题文字即可即时修改围栏名称。
- 🖱️ **完美交互体验**：
  - **原生拖放**：完美支持将桌面图标直接拖入或移出围栏，并支持在围栏间移动。
  - **自动布局**：内置高性能流式布局（Flow Layout），图标自动排列，整洁有序。
- 🛡️ **数据安全保障**：
  - **防抖保存**：采用“操作停顿 1 秒后自动保存”的策略，确保配置实时持久化。
  - **异常容灾**：即使程序非正常关闭，也能在下次启动时完美恢复之前的布局和坐标。
- 📽️ **极致屏幕适配**：全原生支持 **高 DPI 缩放**，在 2K/4K 屏幕上，图标和文字依然清晰锐利。

## 🛠️ 技术选型

- **核心框架**：C++ 17, Qt 5.15+
- **关键技术**：
  - **WinAPI**：深度集成 DWM 属性设置，实现原生阴影与圆角。
  - **Qt Graphics**：高效的 UI 渲染与自定义控件。
  - **JSON 存储**：轻量级的数据序列化方案。

## 🚀 快速上手

### 编译环境
- **IDE**: Qt Creator
- **编译器**: MinGW 64-bit (推荐 8.1.0 及以上)
- **依赖库**: Qt Core, Qt Gui, Qt Widgets, Qt Winextras

### 构建执行
1. 克隆本仓库到本地。
2. 使用 Qt Creator 打开 `DeskGo.pro`。
3. 执行 `qmake` 并进行构建（Build）。
4. 运行 `bin` 目录下的 `DeskGo.exe`。

## 📸 运行预览

*(此处可添加您的屏幕截图)*

- **托盘菜单**：采用自定义 `QWidgetAction` 实现的完美居中交互菜单。
- **外观定制**：支持通过 `app.ico` 和 `app.png` 轻松自定义程序图标。

## 📜 许可证

本项目基于 [MIT License](LICENSE) 协议。

---

<div align="center">
  做出更美观、更实用的桌面整理工具。
</div>
