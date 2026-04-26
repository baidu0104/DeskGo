---
description: DeskGo MSIX 打包与自签名自动化工作流
---

# DeskGo MSIX 打包 Workflow (v2.0 - 高可靠版)

本工作流自动同步 `main.cpp` 的版本定义至 `AppxManifest.xml` 并执行 MSIX 封装与签名。

### 1. 提取版本号与智能规范化
**操作说明：** 动态获取版本号，并处理为符合 MSIX 规范的四段式格式。
// turbo
```powershell
$mainFile = "e:\DeskGo-main\main.cpp";
$versionMatch = (Select-String -Path $mainFile -Pattern 'setApplicationVersion\("(.+?)"\)');
$vRaw = $versionMatch.Matches[0].Groups[1].Value;

$segments = $vRaw.Split('.');
while ($segments.Count -lt 4) { $segments += "0" }
$v4Digits = $segments[0..3] -join '.'; 

Write-Output "[Workflow] 提取版本: $vRaw -> 规范版本: $v4Digits";
```

### 2. 自动同步至应用清单 (精准模式)
**操作说明：** 使用锚点正则精准修改 `<Identity>` 节点的版本号，确保不损坏 XML 声明及中文编码。
// turbo
```powershell
$vRaw = (Select-String -Path e:\DeskGo-main\main.cpp -Pattern 'setApplicationVersion\("(.+?)"\)').Matches[0].Groups[1].Value;
$segments = $vRaw.Split('.');
while ($segments.Count -lt 4) { $segments += "0" }
$v4Digits = $segments[0..3] -join '.';
$manifestPath = "e:\DeskGo-main\bin\AppxManifest.xml";

# 使用 UTF8 编码读取防止乱码
$content = [System.IO.File]::ReadAllText($manifestPath, [System.Text.Encoding]::UTF8);

# 精准替换：仅匹配 Identity 标签内的 Version 属性
$newContent = $content -replace '(?<=<Identity[^>]+)Version="[^"]+"', "Version=""$v4Digits""";

# 强制输出不带 BOM 的 UTF-8 确保 MakeAppx 兼容性
$utf8NoBom = New-Object System.Text.UTF8Encoding($false);
[System.IO.File]::WriteAllText($manifestPath, $newContent, $utf8NoBom);

Write-Output "[Workflow] 已同步 AppxManifest (Version: $v4Digits)";
```

### 3. 执行 MSIX 打包与存储
**操作说明：** 打包 `bin/` 目录。
// turbo
```powershell
$v = (Select-String -Path e:\DeskGo-main\main.cpp -Pattern 'setApplicationVersion\("(.+?)"\)').Matches[0].Groups[1].Value;
$msixPath = "e:\DeskGo-main\DeskGo_v$($v).msix";
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\makeappx.exe" pack /d e:\DeskGo-main\bin /p $msixPath /o;
```

### 4. 执行数字签名
**操作说明：** 对生成的包进行签名（PFX 密码: 123456）。
// turbo
```powershell
$v = (Select-String -Path e:\DeskGo-main\main.cpp -Pattern 'setApplicationVersion\("(.+?)"\)').Matches[0].Groups[1].Value;
$msixPath = "e:\DeskGo-main\DeskGo_v$($v).msix";
& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe" sign /f "e:\DeskGo-main\publisher.pfx" /p 123456 /fd SHA256 /v $msixPath;
```

---

## 🔔 注意
- **编码安全**：本脚本已处理 UTF-8 乱码保护，不可使用记事本手动以其他编码保存 AppxManifest.xml。
- **版本格式**：无论 `main.cpp` 输入 `1.5.9` 还是 `1.5.9.5`，脚本都会自动对齐为 MSIX 标准位。
