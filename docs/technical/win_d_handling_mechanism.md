# Win+D 自定义响应与窗口管理机制

## 1. 问题背景

在开发桌面挂件（Widget）或类似 Rainmeter 的应用时，一个核心痛点是 Windows 系统的 **Win+D**（显示桌面）快捷键行为。默认情况下，Win+D 会向所有顶层窗口发送最小化命令，或者切换到一个特殊的“显示桌面”渲染层，导致桌面挂件也被隐藏或覆盖。

为了实现“挂件钉在桌面上，不随其他窗口最小化”的效果，同时保证用户体验（即其他常规窗口能正常最小化和恢复），我们需要接管并重写 Win+D 的逻辑。

## 2. 核心技术方案

我们的解决方案由三个核心部分组成：
1.  **全局键盘钩子 (Low-Level Keyboard Hook)**：拦截系统级的 Win+D 信号。
2.  **自定义最小化逻辑 (Selective Minimize)**：手动遍历窗口，排除挂件本身，仅最小化用户应用窗口。
3.  **状态追踪与恢复 (State Tracking & Restoration)**：记录被我们最小化的窗口，确保恢复时只还原之前的窗口，避免唤醒无关的后台窗口。

## 3. 实现细节

### 3.1 拦截 Win+D

使用 Windows API `SetWindowsHookEx` 安装 `WH_KEYBOARD_LL` 钩子。

```cpp
// 安装钩子
s_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);

// 钩子回调
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *pKbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        
        // 检测 D 键 (0x44)
        if (pKbdStruct->vkCode == 0x44) {
             // 检测 Win 键状态
            if (GetAsyncKeyState(VK_LWIN) & 0x8000 || GetAsyncKeyState(VK_RWIN) & 0x8000) {
                if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                    // --> 进入自定义处理逻辑
                    handleWinD();
                    
                    // 返回 1 阻止系统默认处理 (即阻止系统执行真正的 Win+D)
                    return 1; 
                }
            }
        }
    }
    return CallNextHookEx(...);
}
```

### 3.2 模式一：显示桌面 (安全最小化)

当检测到 Win+D 且当前不在“显示桌面”模式时：
1.  **清空** 恢复列表。
2.  使用 `EnumWindows` 遍历所有顶层窗口。
3.  **核心过滤**：
    *   **排除自身**：跳过所有属于 DeskGo 的围栏窗口。
    *   **排除 Shell**：跳过任务栏 (`Shell_TrayWnd`)、桌面 (`Progman`, `WorkerW`)。
    *   **排除工具窗口**：跳过 `WS_EX_TOOLWINDOW` 样式的窗口（通常不需要最小化）。
    *   **仅限可见窗口**：只处理 `IsWindowVisible` 的窗口。
4.  **执行操作**：
    *   记录句柄到 `s_windowsToRestore` 列表。
    *   发送 `PostMessage(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0)`。

```cpp
static QList<HWND> s_windowsToRestore;

// 遍历回调
BOOL CALLBACK EnumMinimizeProc(HWND hwnd, LPARAM lParam) {
    // 1. 跳过自身 (FenceWindow)
    if (isMyFenceWindow(hwnd)) return TRUE;

    // 2. 跳过系统 Shell 窗口
    if (isSystemProcessWindow(hwnd)) return TRUE;

    // 3. 仅处理可见且非工具窗口
    if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if ((exStyle & WS_EX_TOOLWINDOW) == 0) {
            
            // 记录句柄以便后续恢复
            s_windowsToRestore.append(hwnd);
            
            // 发送最小化命令
            PostMessage(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        }
    }
    return TRUE;
}
```

### 3.3 模式二：恢复窗口 (精准还原)

当再次按下 Win+D 且当前处于“显示桌面”模式时：
1.  **仅遍历 `s_windowsToRestore` 列表**。
2.  **验证状态**：
    *   窗口句柄是否仍有效 (`IsWindow`)？
    *   窗口是否仍处于最小化状态 (`IsIconic`)？(防止用户在模式期间手动还原了窗口，我们就不该再去动它)
3.  **执行操作**：发送 `SC_RESTORE` 指令。
4.  **清空状态**：操作完成后清空列表，重置模式。

**此改进的重要性**：
早期的实现如果遍历系统所有最小化窗口并还原，会导致输入法隐藏窗口、后台更新程序窗口等“幽灵窗口”弹出。**状态追踪**确保了我们只还原我们亲手隐藏的窗口，实现了“谁污染，谁治理”。

```cpp
// 恢复逻辑
for (HWND hwnd : s_windowsToRestore) {
    if (IsWindow(hwnd) && IsIconic(hwnd)) {
         PostMessage(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
    }
}
s_windowsToRestore.clear();
```

## 4. 总结与优势

| 方案 | 描述 | 缺点 |
| :--- | :--- | :--- |
| **默认行为** | 不做处理 | 挂件随窗口一起消失，体验极差。 |
| **嵌入 WorkerW** | 将父窗口设为 Program/WorkerW | 代码复杂，且在 Windows 更新或壁纸切换时易失效；Win+D 仍可能导致 Z 轴混乱。 |
| **强制置顶 (TopMost)** | 设为 AlwaysOnTop | 会遮挡全屏应用（如游戏、PPT），且不仅防 Win+D。 |
| **本方案 (Hook + 模拟)** | **拦截 Win+D 并手动管理窗口** | **优点**：<br>1. 精确控制：挂件完美驻留。<br>2. 无副作用：不遮挡全屏游戏。<br>3. 干净还原：不唤醒后台幽灵窗口。<br>**缺点**：需要维护 Hook，稍微增加代码复杂度。 |

通过这种机制，DeskGo 实现了原生级的桌面集成体验，既像壁纸一样“钉”在底部，又像普通窗口一样灵活响应。
