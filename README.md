# TodoApp

纯 C + Win32 API 编写的原生 Windows 桌面程序，无 WebView、无第三方 UI 框架、无运行时依赖，单个 exe 约 312 KB（319,488 B）。

界面全部自绘：自绘标题栏、自绘按钮/下拉框/四象限卡片/环形图/趋势图，浅色与深色两套主题。

## 版本

当前 **v1.0.0**。exe 内含完整版本资源（文件版本 `1.0.0.0`、产品名 `TodoApp`、描述、版权）。

发行版下载：<https://github.com/eagler007/TodoApp/releases>

## 功能

### 待办

- 2×2 四象限（紧急 / 重要 两轴），每格独立滚动，右键可跨象限移动
- 单击选中、双击编辑；左侧圆圈直接勾选完成
- 一次性任务，不做重复；软删除进回收站，30 天后自动清理
- 每个条目可挂提醒，到点弹出置顶提示框；程序没运行时由启动补报兜底
- 倒计时条（时/分/秒），Tab 在三个输入框间循环并全选，输入即覆盖

### 记账

- 8 个分类（餐饮 / 购物 / 交通 / 生活 / 娱乐 / 人情 / 医疗 / 其他），固定配色
- 分类预算，超支标红
- 统计报表：当期分类统计（环形图）+ 月度报表 + 12 个月趋势（贝塞尔曲线 + 面积）
- 收入 / 支出双模式

### 其他

- 标题栏显示「城市 天气现象 温度」，双击可改城市；共 121 个城市（国内 46 / 亚洲 18 / 欧洲 25 / 北美 14 / 南美 6 / 大洋洲 6 / 非洲 6）
  - 国内走中国天气网接口，国外走 Open-Meteo（含空气质量 AQI），双数据源自动切换
- 系统托盘：显示 / 隐藏、开机自启、退出
- 开机自启写 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，可在托盘菜单开关

## 构建

### 依赖

只有 MinGW-w64 工具链（gcc + windres），无其他第三方库。图形部分用系统自带的 GDI 与 GDI+。

本项目在 Windows 上用 [w64devkit](https://github.com/skeeto/w64devkit)（GCC 16.2）验证通过。

### 方式一：构建脚本

```sh
sh _build_local.sh      # 产出 simple-todo-c/TodoApp.exe
sh _build_selftest.sh   # 产出 simple-todo-c/TodoSelfTest.exe
```

脚本里有两行绝对路径（工具链 PATH 与工程目录），换机器时需要按本机位置调整。

### 方式二：Makefile

```sh
cd simple-todo-c
make
```

`Makefile` 使用带前缀的交叉编译器名（`x86_64-w64-mingw32-gcc`）。若工具链没有前缀（如 w64devkit），把 `CC` / `WINDRES` 改成 `gcc` / `windres`。

链接库：`comctl32 shell32 comdlg32 uuid wininet gdi32 gdiplus advapi32`。

## 运行

直接运行 `simple-todo-c/TodoApp.exe` 或 `dist/TodoApp.exe`。

命令行参数：

| 参数 | 行为 |
|---|---|
| `--tray` | 启动后直接隐藏到托盘（开机自启用） |

单实例：重复启动会复用已运行实例。

## 数据存储

位置：`%APPDATA%\TodoApp\`

| 文件 | 内容 |
|---|---|
| `todos.json` | 待办 |
| `accounts.json` | 记账流水 |
| `budgets.json` | 分类预算 |
| `config.ini` | 城市、主题、开机自启等设置 |
| `backups/<stem>-YYYYMMDD.json` | 每日自动备份 |

日期在存储层统一为 `YYYY/MM/DD`。加载失败时程序进入只读模式并提示，不会覆盖原文件。

## 自检

```sh
cd simple-todo-c
./TodoSelfTest.exe
echo $?     # 返回值 = 失败断言数，0 表示全过
```

当前 **74 条断言**，覆盖存储读写、软删除与清理、日期归一化、分类统计、预算计算、JSON 字段提取等。纯控制台，不弹窗。

## 目录结构

```
simple-todo-c/          源码与构建产物
  main.c                入口、消息循环、Tab 焦点循环
  app.c                 主窗消息处理、提醒、托盘
  app_ui.c              主界面布局与自绘
  ui.c                  自绘控件库（按钮、输入框、下拉框、四象限）
  dlg.c                 对话框框架与自绘提示框
  menu.c                自绘右键菜单与下拉弹出层
  theme.c               浅色 / 深色主题
  calendar.c            月历与时间选择
  pie.c                 环形图（GDI+）
  trend.c              12 个月趋势图
  weather.c             121 城天气，双数据源
  startup.c             开机自启（注册表）
  todo.c / account.c    数据模型
  storage.c             JSON 持久化、备份、软删除
  config.c              config.ini 读写
  selftest.c            74 条断言
  lib/cJSON.c           内嵌 cJSON（MIT）
dist/                   交付物：exe ×2 + 51 张界面预览
_build_local.sh         主程序构建
_build_selftest.sh      自检构建
界面改造-交付说明.md     改造过程与踩坑记录（41 节）
```

## 已知边界

- 提醒依赖进程存活：没登录、手动退出、休眠期间不会响，只能在下次启动时补报（不做 Windows 计划任务）
- 提醒是一次性的，不重复触发
- 待办除倒计时外不按日期过滤，`created_at` 仅作记录
- 天气需要联网；断网时显示上一次结果，不报错

## 许可

私有项目，未声明开源许可。
