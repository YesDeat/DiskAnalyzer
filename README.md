# DiskAnalyzer

磁盘空间分析与一键清理工具，扫描 C/D 等盘符后，按「系统 / 程序 / 游戏 / 缓存 / 开发 / 下载 / 影音 / 文档 / 回收站」自动归类，并内置常用清理入口。

A disk space analyzer with one-click cleanup. It scans a drive and automatically groups everything into System / Programs / Games / Cache / Dev / Downloads / Media / Documents / Recycle Bin, and ships the common cleanup shortcuts.

## 编译 / Build

1. 用 Visual Studio 2022（工具集 v143）打开 `DiskAnalyzer.sln`
2. 选择 **Release | x64**
3. 生成解决方案

1. Open `DiskAnalyzer.sln` with Visual Studio 2022 (toolset v143)
2. Choose **Release | x64**
3. Build the solution

> 程序启动时会检测管理员权限，没有则自动申请提权，用于读取受保护的系统目录。
> The app checks for administrator privileges on startup and requests elevation if needed, so it can read protected system directories.

## 界面说明 / UI

| 区域 / Area | 说明 / Description |
| --- | --- |
| 驱动器 Drive | 下拉框自动列出所有本地固定磁盘，并显示可用/总容量<br>Lists all fixed local drives with free/total capacity |
| 分类汇总 Breakdown | 各类别占用大小、百分比（右侧彩色条）、文件数<br>Size, percentage (colored bar) and file count per category |
| 占用最大的目录 Largest folders | 前两层目录中体积最大的 300 个，按大小降序<br>Top 300 largest folders from the first two directory levels |
| 一键清理 One-click cleanup | 六个清理入口，见下表<br>Six cleanup shortcuts, see below |

## 清理按钮 / Cleanup buttons

| 按钮 / Button | 作用 / What it does |
| --- | --- |
| 清理临时文件 Clean temp | 清空 `%TEMP%`、`C:\Windows\Temp`、`INetCache`、`CrashDumps`、`WER`、资源管理器缩略图缓存 |
| 清理更新缓存 Update cache | 清空 `C:\Windows\SoftwareDistribution\Download` |
| 清空回收站 Recycle bin | 调用系统 API 清空所有盘的回收站 |
| 磁盘清理 cleanmgr | 以管理员身份打开系统「磁盘清理」，进去后记得点**「清理系统文件」**才能看到 Windows 更新清理和 Windows.old |
| 存储感知 Storage Sense | 直接打开 `设置 → 系统 → 存储 → 存储感知`，可设为每天自动清理 |
| 导出报告 Export report | 把当前分析结果保存为桌面上的 `DiskAnalyzer_Report.txt` |

> 删除时被占用的文件会自动跳过，不会强行终止进程。
> Files that are in use are skipped automatically; no process is killed.

## 分类规则 / Classification rules

规则集中在 `main.cpp` 顶部的两张表里，**按顺序匹配，命中第一条即返回**：

The rules live in two tables at the top of `main.cpp`. **They are evaluated top to bottom and the first match wins**:

- `g_pathRules[]` —— 路径关键字，例如 `:\windows\`、`\appdata\local\temp\`、`\steamapps\`、`\node_modules\`
- `g_extRules[]` —— 扩展名，例如 `.tmp`、`.log`、`.pak`、`.mp4`、`.docx`

想调整归类，直接在这两张表里增删条目即可，不需要改逻辑代码。

To tweak the grouping, just add or remove entries in those two tables — no logic changes needed.

几点说明 / Notes:

- 游戏规则的优先级放在缓存规则**之前**，这样 Steam 着色器缓存会被算进游戏而不是缓存。
  Game rules are placed **before** cache rules so Steam shader caches count as games, not cache.
- 符号链接和挂载点（reparse point）会被跳过，避免扫描时死循环。
  Reparse points (symlinks / junctions) are skipped to avoid infinite recursion.
- 扫描使用 `\\?\` 前缀，支持超过 260 字符的长路径。
  Scanning uses the `\\?\` prefix, so paths longer than 260 characters work.

## 文件说明 / Files

| 文件 / File | 说明 / Description |
| --- | --- |
| `main.cpp` | 扫描引擎、分类规则、清理逻辑、界面<br>Scan engine, classification rules, cleanup logic, UI |
| `resource.h`、`DiskAnalyzer.rc` | 资源定义与资源脚本（图标）<br>Resource definitions and script (icon) |
| `DiskAnalyzer.vcxproj`、`.filters`、`.sln` | Visual Studio 工程文件<br>Visual Studio project files |
| `001.ico` | 程序图标 / Application icon |
