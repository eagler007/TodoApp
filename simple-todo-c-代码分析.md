# simple-todo-c 代码分析报告

> 源站：`192.168.2.13:/home/Todo/simple-todo-c`（CentOS 8 / kernel 4.18，交叉编译托管）
> 本地副本：`D:\WorkBuddy\Todo程序\simple-todo-c\`
> 分析时间：2026-10-23

---

## 一、结论先行

这是一个 **用纯 C + Win32 API 手写 GUI 的 Windows 桌面待办 + 记账双 Tab 应用**，约 1900 行有效 C 代码，外加一个 1700 行的 tkinter 前身 `todo.py`。

三个关键判断：

1. **工程结构清晰、编译零错误**。`make` 全量构建通过（`x86_64-w64-mingw32-gcc`，仅 2 个无害的 `-Wmissing-field-initializers` 警告）。分层干净：UI / 领域 / 存储 / 工具四层，无全局变量滥用（只有 1 个字体缓存和 2 个 `static` 窗口指针）。
2. **最大隐患不在语法层，在持久化层**：`storage.c` 直接 `fopen("wb")` 截断写，没有临时文件 + 原子替换；也没有实例互斥。断电或双开即丢全量数据。
3. **与 `todo.py` 共用同一份 `%APPDATA%\TodoApp\todos.json`，但字段名不兼容**，任一版本保存都会抹掉另一版本的字段。这是当前最实际的数据风险。

---

## 二、模块职责

| 文件 | 行数 | 职责 |
|---|---|---|
| `main.c` | 57 | `WinMain`：注册 2 个窗口类（主窗 + 通用面板）、建菜单、跑消息循环 |
| `app.c` | 1369 | 全部 UI 与业务编排：控件创建、列表刷新、右键菜单、5 个对话框、托盘、倒计时、提醒 |
| `todo.c/h` | 60 | `Todo` 领域结构 + 动态数组（16 起、倍增扩容、`memmove` 删除） |
| `account.c/h` | 88 | 记账领域结构 + 同样的动态数组 + 按月份/分类聚合 |
| `storage.c` | 144 | JSON 序列化/反序列化，读写 `todos.json` / `accounts.json` |
| `util.c` | 107 | 时间格式化、日期解析、UTF-8 ↔ UTF-16 转换、日期规范化 |
| `lib/cJSON.c` | — | 第三方单文件 JSON 库（直接 vendored，未改） |
| `resource.rc` | 70 | 图标 + 5 个对话框模板 + manifest（UAC 提权/DPI 声明） |

分层依赖是单向的：`main → app → {todo, account, storage} → util → cJSON`，没有反向依赖，也没有循环包含。

---

## 三、架构要点（值得学的 5 个手法）

**1. 用"空壳资源 + 代码填文本"绕开 .rc 的中文编码坑**
`resource.rc` 里所有 `CAPTION ""`、`LTEXT ""`、按钮文本全为空，实际文案在 `WM_INITDIALOG` 里用 `SetWindowTextW` 补。原因是 `windres` 处理 UTF-8/GBK 中文极易乱码，作者选择把中文全部收进 C 源码（UTF-8）统一转换。这是实用的妥协，代价是对话框在资源编辑器中不可视化编辑。

**2. 自定义面板窗口 + 消息转发，实现 Tab 键导航**
两个 Tab 页各是一个 `TodoPanel` 自定义窗口类。`PanelWndProc` 把 `WM_COMMAND / WM_NOTIFY / WM_CTLCOLOR* / WM_DRAWITEM` 直接转发给父窗口处理，自己只负责 `WM_ERASEBKGND` 画灰底。主消息循环里对**当前可见面板**调用 `IsDialogMessageW`，从而免费获得 Tab 焦点跳转。面板句柄通过 `GWLP_USERDATA` 传给消息循环——是个 hack，但只占一行。

**3. 列表行用 `lParam` 存业务 id，而不是行号**
`LVIF_PARAM` 存 `todo.id`。这样过滤、排序之后取到的仍是稳定业务键，避免了"删完一行索引全错"这类经典 bug。所有增删改查都走 `id`，`todo_list_find` 线性查找（数据量 <100 时完全可以接受）。

**4. 双缓冲 ListView + 单例字体缓存**
`LVS_EX_DOUBLEBUFFER` 消除重绘闪烁；`GetDefaultFont(10)` 首次创建后缓存到 `g_font`，避免每次 `SetCtrlFont` 泄漏 GDI 对象。

**5. 三个定时器各司其职**
`TIMER_CLOCK`(60s) 刷新状态栏时钟、`TIMER_COUNTDOWN`(1s) 递减倒计时、`TIMER_REMINDER`(5s) 扫提醒。全部挂在主窗口，单线程无锁。

---

## 四、数据模型与持久化

两个 JSON 文件，均为顶层数组：

```
%APPDATA%\TodoApp\todos.json     -> [{id,title,content,completed,created_at,reminder}]
%APPDATA%\TodoApp\accounts.json  -> [{id,record_type,amount,category,date,note,created_at}]
```

- 日期双格式并存：`date` 存 `YYYY/MM/DD`，`created_at` 存 ISO `YYYY-MM-DDTHH:MM:SS`。读取时 `normalize_date()` 把 `-` 统一成 `/`。
- `id` 由 `next_todo_id` / `next_acct_id` 单调递增分配，加载时取最大值 +1 恢复。**删除后 id 不复用**，符合预期。
- 备份文件是**对象**而非数组：`{"todos":[...], "accounts":[...]}`，导入时会重新分配 id 以避免主键冲突。备份格式与主存格式不一致，是有意的设计。

### 与 `todo.py` 的格式冲突（重要）

| 概念 | `todo.py` 字段 | `simple-todo-c` 字段 |
|---|---|---|
| 正文 | `description` | `content` |
| 提醒时间 | `reminder_at` | `reminder` |
| 优先级 | `priority` | **无** |
| 完成时间 | `completed_at` | **无** |

两者写同一个 `todos.json`。C 版读 Python 的数据时 `content` 读不到（空字符串），保存时按 C 的 schema **整文件重写**，Python 的 `description` / `priority` / `completed_at` 就此永久消失。反过来也一样。二者不能混用同一份数据目录。

---

## 五、问题清单

> **修复状态（本轮更新）**：1 / 2 / 4 / 8 / 9 已修，另外补了 3 个本报告没列出的问题。
> 详见 `界面改造-交付说明.md` 第 17–25 节。
>
> | # | 状态 | 怎么修的 |
> |---|---|---|
> | 1 截断写 | ✅ | `.tmp` 写 → `fflush` + `_commit` → `CopyFileW` 出 `.bak` → `MoveFileExW` 原子替换 |
> | 2 无单实例 | ✅ | `Local\TodoApp.SingleInstance` 互斥体，重启时激活已有窗口 |
> | 4 `MessageBoxA` 乱码 | ✅ | 全部换成自绘 `Ui_MsgBox`（宽字符 + 主题色） |
> | 8 只有支出 | ✅ | `record_type` 接 UI：支出/收入分段按钮 + 报表三行汇总 |
> | 9 自重叠 `snprintf` | ✅ | 先拼到临时缓冲再 `strncpy` 回目标 |
> | — 读取失败当空数据 | ✅ 新增 | 返回值改三段语义，`-1` 进只读模式 + 按天备份 |
> | — 删除不可撤销 | ✅ 新增 | 软删除 + 回收站 + 30 天自动清理 |
> | — 无预算约束 | ✅ 新增 | `budgets.json` + 报表预算列 + 超支整行标红 |
>
> 未修：3（数据格式归属，需老板决定）、5 / 6 / 7（编辑框长度、DateTimePicker 通知、
> 提醒补发）、10–16（代码卫生类）。

### 高危

| # | 位置 | 问题 |
|---|---|---|
| 1 | `storage.c:22 write_file` | 直接 `fopen(path,"wb")` 截断后写入。写一半崩溃/断电 → 整个 JSON 变空 → 全部数据丢失。应写临时文件再 `rename()` 原子替换 |
| 2 | 全局 | 无单实例保护（无 Mutex）。同时开两个窗口，后关的那个用内存里的旧数据覆盖前一个的修改 |
| 3 | 数据格式 | 与 `todo.py` 共用数据目录但 schema 不兼容，见上表 |

### 中危

| # | 位置 | 问题 |
|---|---|---|
| 4 | `app.c` 6 处 `MessageBoxA` | 传入的是 UTF-8 中文字面量，`MessageBoxA` 按系统 ANSI 代码页（GBK）解释 → 中文提示必然乱码。应改用 `MessageBoxW` + `L"..."` |
| 5 | `app.c:718-719`、`960-962` | 编辑对话框用 `WCHAR wbuf[256]` 中转，而 `content` 上限是 1024 字节。长内容编辑时被静默截断到约 256 个宽字符 |
| 6 | `app.c:818` | 对 DateTimePicker 调 `SetWindowTextW` 无效（该控件不响应 WM_SETTEXT）。应发 `DTM_SETSYSTEMTIME` |
| 7 | `app.c:516 CheckReminders` | 提醒触发后立刻清空并落盘，程序未运行期间到期的提醒**永久丢失**，无补发、无逾期回看 |
| 8 | `account` 全链路 | `record_type` 字段存在且默认 `"expense"`，但 C 版 UI 没有收/支切换，月度报表把所有记录当支出累加。Python 版有该功能，移植时丢了 |

### 低危 / 代码卫生

| # | 位置 | 问题 |
|---|---|---|
| 9 | `util.c:71` | `snprintf(buf, bufsize, "%s-%s", buf, mon)` —— 源缓冲和目标缓冲是同一块内存，属未定义行为。当前因为年份恒为 4 位且写入位置同步推进，**恰好**结果正确；一旦出现 `99/01/01` 这类输入就会出错 |
| 10 | `app.c:1358` | `APP_MIN_WIDTH/HEIGHT` 定义了但从未使用，窗口可缩到 0×0，控件被压扁 |
| 11 | `todo.c:67` `todo_list_sort_by_date`；`util.c` 的 `parse_date` / `format_date` / `is_valid_date` / `strdup_a` | 全是死代码，无任何调用点 |
| 12 | `app.c:136` | `acct_list_sort_by_date_desc(list)` 在**视图刷新函数**里调用，会修改数据本身顺序。排序应在加载/新增时做，不应放在渲染路径 |
| 13 | `app.c:759,767,837` | 对话框返回后无条件 `SaveXxx()`，即使点了"取消"也写盘（数据没变，但白白重写文件） |
| 14 | `app.c:536` | `wcsncpy(nid.szInfoTitle, wtitle, 64)`，`szInfoTitle` 长度为 64，恰好 64 字符时无终止符 |
| 15 | `app.c:40-52` | `APPDATA` 取不到时数据落在 exe 同目录，且**不带** `\TodoApp` 子目录，与正常路径不一致 |
| 16 | `README.md` | 与实际严重不符：说编译用 `build.bat` 输出 `bin\todo.exe`（实际是 `Makefile` → `TodoApp.exe`），说数据存 `todos.dat` 二进制（实际是 JSON） |

### 无问题项

`todo.c` / `account.c` 的动态数组实现（扩容、`memmove` 删除、`qsort` 比较函数）正确；`storage.c` 的 cJSON 生命周期管理（`cJSON_Print` 结果用 `free`）正确；UTF-8/UTF-16 转换点选择正确；资源与图标加载路径正确。

---

## 六、`todo.py` 的定位

`todo.py`（1700 行，tkinter）是 C 版的**前身**，功能反而更全：

- 开机自启（写注册表 `Run` 项，`enable_auto_start` / `disable_auto_start`）
- `pystray` 托盘 + `PIL` 图标，右键菜单含"显示/开机自启/退出"三态
- 输入框 placeholder（`on_input_focus_in/out` 手工实现）
- 收支类型切换、优先级、完成时间
- 通知走 PowerShell 内联脚本调 `System.Windows.Forms.NotifyIcon`（避开 `win10toast` 依赖）

C 版为了 200KB 单文件 exe、零运行时依赖，砍掉了其中 4 项：开机自启、优先级、收支类型、完成时间。**文件时间线印证了这一点**：`todo.py` 最后修改 06-02 12:25，同一天 12:28 起 `account.c`/`app.h`/`Makefile` 陆续出现，06-26 完成 `app.c` 并构建发布。

迁移是"功能减法 + 性能加法"：体积从 PyInstaller 的数十 MB 降到 200KB，启动从秒级到毫秒级，代价是丢掉上面 4 个功能。

---

## 七、接手建议（按性价比排序）

1. **先修 `write_file`**：临时文件 + `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING)`。改动 <20 行，消除最大的数据丢失风险。
2. **修 6 处 `MessageBoxA`** → `MessageBoxW`。纯机械替换，立刻消除乱码。
3. **加单实例 Mutex**，或在数据目录放 `.lock` 文件。防双开覆盖。
4. **明确数据格式归属**：要么让 C 版兼容 `description`/`reminder_at` 作为别名，要么给 C 版换独立数据目录（如 `TodoApp-C`），二选一。
5. 补 `record_type` 的 UI 切换，否则记账本只能记支出，月度报表口径失真。
6. 清理死代码、更新 `README.md`。

---

## 附：构建命令

```bash
# 服务器上（CentOS 8，已装 mingw 交叉工具链）
cd /home/Todo/simple-todo-c && make          # 产出 TodoApp.exe
make clean                                    # 清理

# 发布包
unzip -l TodoApp.zip     # 仅含 TodoApp.exe + todo.ico
```

工具链版本：`x86_64-w64-mingw32-gcc`（GCC 8.5.0）、`x86_64-w64-mingw32-windres`、`make`、`git` 均可用；目录**未初始化 git**，无版本历史，改动前请先备份。
