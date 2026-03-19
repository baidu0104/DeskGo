# DeskGo MSIX 打包与签名指南

本文档总结了 DeskGo 项目的 MSIX 打包流程、工具路径及必要凭据。在更新 `bin` 目录内容后，请按照以下步骤重新打包。

## 🛠 工具路径 (Windows 10 SDK)

打包和签名工具位于以下路径（已验证）：
- **MakeAppx.exe (打包)**: `C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\makeappx.exe`
- **SignTool.exe (签名)**: `C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe`

## 🔐 证书信息
- **证书文件**: `e:\DeskGo-main\publisher.pfx`
- **PFX 密码**: `123456`

## 🚀 打包步骤

### 1. 执行打包
将 `bin` 目录下的所有内容打包成 `.msix` 文件：
```powershell
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\makeappx.exe" pack /d e:\DeskGo-main\bin /p e:\DeskGo-main\DeskGo_v1.5.8.msix /o
```

### 2. 数字签名
对生成的 `.msix` 文件进行签名（必须签名才能在 Windows 上安装）：
```powershell
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe" sign /f e:\DeskGo-main\publisher.pfx /p 123456 /fd SHA256 /v e:\DeskGo-main\DeskGo_v1.5.8.msix
```

## ⚠️ 注意事项
1. **AppxManifest.xml**: 打包前确保 `bin/AppxManifest.xml` 中的版本号与项目一致。
2. **自启控制**: 为避免 MSIX 强制自启，`AppxManifest.xml` 中不应包含 `<desktop:Extension Category="windows.startupTask">` 节点，当前项目已移除该节点以实现代码控制自启。
