# 环境变量管理模块化 — 实施计划（定稿）

> 状态：**已全部确认锁定**
> 分支：`arena/3e777a46-chsrc`（基于 `feat/env`）
> 范围：前置基础设施修复（P0） + 新增 framework 层环境变量模块 `env.c`（P1–P2） + 统一迁移 5 个现有 env recipe（P3） + 测试与文档（P4）
> 原则：未列入本计划的事一律按"不做"处理，不预留扩展点

---

## 0. 目标 / 非目标

### 目标

1. 把散落在 recipe 里的环境变量读写收敛成 `src/framework/env.c` 一处可复用实现，recipe 贡献者不再手写平台与 shell 方言分支。
2. 修掉实际缺陷：现有 5 个 env recipe（`rustup`、`Pub`、`Flutter`、`nvm`、`Homebrew`）全部只 `chsrc_append_to_file`，重复 `chsrc set` 无限堆积脏行。
3. 关掉命令注入（RCE）入口：
   - Windows 侧废除 `setx NAME "<url>"` 字符串拼接，改用 Win32 注册表宽字符 API，引号注入面归零；
   - POSIX 侧严格拦截双引号内仍会被 bash/zsh/fish 求值的 `$`、`` ` ``、引号、反斜杠与换行符。
4. 让 `chsrc get`（`chsrc_view_env`）精确匹配并优先报出**持久化值**，同时对比当前终端继承的临时值给出明确提示；多文件冲突或复杂动态语法无法唯一定位时拒绝盲猜并告知原因。
5. 在开始新模块实现前，以独立前置 commit 修掉框架及 `lib/xy.h` 中已识别的底层函数缺陷与平台宏混用问题（见第 1 节）。

### 非目标

- 不做 `PATH` 式"列表追加"语义（当前 0 个 recipe 需要）。
- 不做项目级（`ProjectScope`）环境变量的实际落地。
- 不引入 `block.c`、`ProgStatus.CurrentDish` / `CurrentSource`，不在 POSIX 配置文件中写入 `# Ver2 CHSRC BLOCK ...` 注释包裹头尾，也不主动清扫历史遗留的 `# ------ chsrc BLOCK BEGIN/ENDIN ------` 等注释痕迹（仅折叠同文件内重复的同名变量赋值行，并对 `config.fish` 中阻塞生效的 5 个迁移 dish 旧变量赋值行做一次性精确清理）。
- 不动 `Arch-Linux.c` 那类非环境变量的"配置文件换源"。

---

## 1. 需提前修复的既有错误 / 不完善函数清单（Phase P0）

以下既有问题直接影响本模块的正确性、安全性或跨平台一致性，按职责拆分为独立前置 commit 先行修复：

| 序号 | 位置 | 既有问题 / 缺陷现象 | 前置修复方案 |
|---|---|---|---|
| **F1** | `src/framework/OS.c:280-299`<br>`chsrc_ensure_root ()` | 1. `getenv ("$EUID")` 多写了 `$`，恒为 `NULL`，导致每次必 fork `id -u`。<br>2. **严重提权误判漏洞**：`xy_str2int (buf)` 在 `buf == NULL` 或非数字时返回 `0`（见 `lib/xy.h:505`），一旦 `id -u` 执行失败（如 Windows 或精简容器环境），`0 != xy_str2int(buf)` 为假，**直接误判当前用户是 root**！<br>3. 缺少非中止的权限探测函数，且 Windows 无管理员组判定与对应提权文案。 | 1. 新增 `bool chsrc_is_root (void)`：POSIX 下直接调 `<unistd.h>` 的 `geteuid () == 0`（零 fork、无字符串解析歧义）；`#ifdef XY_Build_On_Windows` 下用 `AllocateAndInitializeSid` + `CheckTokenMembership` 检测 `DOMAIN_ALIAS_RID_ADMINS`。<br>2. `chsrc_ensure_root ()` 改为调用 `chsrc_is_root ()`，失败时按平台输出双语指引（Windows 提示以管理员身份运行终端，POSIX 提示 `sudo`/切换 `root`）并 `exit (Exit_UserCause)`。 |
| **F2** | `src/framework/OS.c:748-868`<br>`chsrc_overwrite_file ()`<br>`chsrc_append_to_file ()`<br>`chsrc_prepend_to_file ()` | 1. **空串写入必崩**：`chsrc_overwrite_file` 与 `chsrc_append_to_file` 执行 `fwrite (str, len, 1, f)` 后断言 `ret == 1`；按 C11 标准，当 `len == 0`（如删掉文件中唯一一条环境变量后覆写空串）时 `fwrite` 返回 `0`，误触发"写入文件失败"并 `exit (Exit_UserCause)`。<br>2. `chsrc_append_to_file` 在 `fwrite` 失败分支漏掉了 `fclose (f)`。<br>3. `chsrc_prepend_to_file` 未调用 `chsrc_ensure_dir`、未校验 `fwrite` 返回值、报错硬编码中文 `"文件打开失败"`。 | 单独一个 commit 改进文件写入族函数：`len > 0` 时才执行 `fwrite` 与 `ret != 1` 校验（`len == 0` 时打开 `"w"` 已完成截断清空）；补齐 `fclose (f)`、`chsrc_ensure_dir` 及中英双语错误信息。 |
| **F3** | `src/framework/OS.c:873-948`<br>`chsrc_backup ()` | 未对入参 `path` 先做 `xy_normalize_path`：`ComboBackedUpPaths` 去重栈直接比较原始字符串（`"~/.bashrc"` 与展开后绝对路径可能不一致）；Windows 分支 `copy /Y <path> <path>.bak` 遇到 `~` 或正斜杠 `/` 会直接失败；路径含空格时 shell 命令未加引号保护。 | 对 `path` 先通过 `xy_normalize_path` 规范化用于去重与实际文件拷贝（日志仍展示直观路径），并在 shell 拷贝命令中对路径加引号保护。 |
| **F4** | `lib/xy.h:111,1405,1553`<br>`src/framework/OS.c:407,495,957,1012` | **`XY_Build_On_Windows` 与 `xy.on_windows` 用法混乱且存在逻辑断层**：<br>1. `lib/xy.h:1553` `_xy_detect_os()` 通过探测 `%SystemDrive%\Windows` 在运行期置 `xy.on_windows = true`；在 Cygwin/MSYS POSIX 编译器下 `_WIN32` 未定义（无 `XY_Build_On_Windows`），却会导致 `xy.on_windows == true`，进而使 `lib/xy.h:1405` `xy_dir_exist()` 落入空 `#ifdef` 分支**恒返回 `false`**。<br>2. `OS.c:957,1012` 写了 `#if XY_Build_On_Windows`（非 Windows 下宏未定义），而 `OS.c:407` 写 `#ifdef XY_Build_On_Windows`。<br>3. `OS.c:495` 在 `#ifdef XY_Build_On_Windows` 块外用运行期 `if (xy.on_windows && !tmpfilepath)` 触发 `xy_unreached()`。 | 独立前置 commit 同步修正 `lib/xy.h` 与 `src/framework/OS.c`：<br>- 在 `lib/xy.h` 中使 `_xy_detect_os()` 的 Windows 判定严格受 `#ifdef XY_Build_On_Windows` 约束（Cygwin/MSYS POSIX 运行时归入 Unix 分支），消除 `xy.on_windows` 与 `XY_Build_On_Windows` 的分裂，修复 `xy_dir_exist()`。<br>- **编译期 `#ifdef XY_Build_On_Windows`**：全仓统一用 `#ifdef`（禁用 `#if`），且**仅用于包裹 Win32 SDK 专有类型与 API 调用**。<br>- **运行期 `if (xy.on_windows)`**：所有纯 C 跨平台分支逻辑一律用 `if (xy.on_windows)`。 |
| **F5** | `src/framework/struct.h:140`<br>`src/framework/chef.c:393`<br>`src/framework/source.c:797`<br>`src/framework/waiter.c:250,882` | 1. `waiter_handle_Reset_Source ()` 漏调 `chsrc_check_scope_capability (dish)`，导致 `chsrc reset -scope=project <dish>` 绕过框架拦截。<br>2. 新增 `ScopeCap_Able_But_Just_Docs` 需同步更新 `struct.h`、`chef_set_default_scope ()`、`chsrc_check_scope_capability ()`、`cli_print_dish_features ()`。<br>3. Scope 检查与 `ls` 状态提示硬编码中文，不支持 `-en`。 | 补齐 `waiter_handle_Reset_Source ()` 的 scope 校验；合入 `ScopeCap_Able_But_Just_Docs` 框架支持（校验与默认作用域均放行，`ls` 显示黄色 `HalfYesMark`，本分支暂不绑定具体 dish）；补齐中英双语文案。 |
| **F6** | `Makefile:61-67,168-170,240,245`<br>`justfile:59-88` | 1. 现有 `OS.c` / `xy.h` 已调用 `kernel32` / `shell32` API，但构建脚本依赖编译器隐式链接。<br>2. `Makefile` 在 Windows MSYS2 上交叉编译 Android（`CROSS_BUILD_WINDOWS_FOR_ANDROID=1`）时 `On-Windows` 仍为 `1`，无条件加 Windows 库会打挂 Android Release CI。<br>3. MinGW 单遍链接器要求 `-l*` 位于源文件与 `-static` 之后。 | 在 `Makefile`（加 `ifneq ($(CROSS_BUILD_WINDOWS_FOR_ANDROID), 1)` 守卫）与 `justfile` 中新增尾部链接库变量 `LDLIBS_win = -ladvapi32 -lkernel32 -luser32 -lshell32`，统一置于编译命令行末端（`.c` 文件之后）。 |
| **F7** | `src/chsrc-main.c:171,231` | 1. `chsrc set -scope` 末尾缺参时 `argv[i+1]` 为 `NULL`，传入 `xy_strcat` 导致空指针解引用崩溃；且消费 `argv[i+1]` 后未执行 `i++`。<br>2. 第 231–232 行判断 `-version`/`--version` 误写 `xy_streql (command, ...)` 而非 `argv[i]`，导致 `chsrc -en --version` 报未知选项。 | 独立前置小 commit 补上 `i + 1 <= argc` 边界检查、`i++` 及 `argv[i]` 修正。 |

---

## 2. 已锁定决策汇总

| 编号 | 决策结论 | 工程落地细则 |
|---|---|---|
| **架构与写入模型** | **取消 `block.c` 与注释头；内聚行级精确键值编辑**（C1） | 不建 `block.c`、不加 `ProgStatus.CurrentDish` / `CurrentSource`、不写 `# Ver2 CHSRC BLOCK ...` 注释头尾。`env.c` 内部以 `static` 原语直接按目标方言精确匹配同名环境变量赋值行：已存在则原位改写第 1 条并剔除同文件内后续重复的同名赋值行；不存在则追加；值为 `NULL` 则删除全部匹配赋值行；若匹配行含动态展开/多变量同行等复杂语法则拒绝改写并报错告知文件与行号。用户原有注释及历史 `# ------ chsrc BLOCK BEGIN/ENDIN ------` 注释痕迹一律保留不碰。 |
| **D2 / D31 / Q5** | **Win32 注册表宽字符 API + `REG_SZ`** | 使用全版本通用的 `RegOpenKeyExW` / `RegQueryValueExW` / `RegSetValueExW` / `RegDeleteValueW` / `RegCloseKey`；写入类型固定为 `REG_SZ`（读取时兼容既有 `REG_SZ` 与 `REG_EXPAND_SZ`），在 `env.c` 内提供 `static` UTF-8 ↔ UTF-16LE 转换（`MultiByteToWideChar` / `WideCharToMultiByte`）。 |
| **D3 / Q1** | **Windows 导入库全部显式后置链接** | `Makefile`（排除 `CROSS_BUILD_WINDOWS_FOR_ANDROID=1`）与 `justfile` 显式后置链接 `-ladvapi32 -lkernel32 -luser32 -lshell32`。 |
| **D4 / D23 / D24 / C3=B** | **POSIX 精确识别当前 shell，无法确认时拒绝执行** | 检测优先级：父进程 `comm`（Linux/Android 读 `/proc/<ppid>/comm`，macOS/BSD 走 `ps -p <ppid> -o comm=`）→ `$SHELL`。<br>- **无法识别当前 shell 时（C3=B）**：`set` 与 `get` 均拒绝盲目执行，报错说明无法确认当前 shell 并给出手工添加指引。<br>- **写入规则**：已知 rc 文件仅在已存在时写入；**`~/.profile` 例外**（bash/zsh 下不存在则创建兜底登录环境）；当前 shell 为 **fish** 时目标为 `~/.config/fish/conf.d/chsrc.fish`（不存在则创建）。<br>- **检查集提醒**：其余已知 shell 配置文件若检测到同名变量，用 `chsrc_note2` 双语提示，不跨 shell 清理。 |
| **D25** | **fish `config.fish` 加载顺序陷阱一次性清理** | fish 先加载 `conf.d/*.fish` 后加载 `config.fish`。对迁移的 5 个 dish 涉及的已知变量名，写入 `conf.d/chsrc.fish` 时若检测到 `~/.config/fish/config.fish` 中存在非注释且语法可精确判定的 `set -x` / `set -gx` 同名行，先 `chsrc_backup` 再精确删除该行。 |
| **D8 / D18 / D33** | **开放 `SystemScope` + 前置权限自检** | POSIX 写 `/etc/environment`，Windows 写 `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment`。执行前先调 `chsrc_is_root ()` 自检权限；不足则不做任何修改、不建备份，直接打印对应平台的提权/手工修改双语指引并 `exit (Exit_UserCause)`。 |
| **D9** | **`ImplementationDefinedScope` → `UserScope`** | 在 env 模块内默认作用域固定解析为 `UserScope`。 |
| **D10 / D28 / Q4 / C4** | **新增 `ScopeCap_Able_But_Just_Docs`，本分支暂置空** | 在框架层完成枚举定义、`chef_set_default_scope` 放行、`chsrc_check_scope_capability` 放行及 `ls` 黄色 `HalfYesMark` 双语展示；本分支不修改具体 dish 使用该枚举。`waiter_handle_Reset_Source` 补齐 scope 校验，`env.c` 内部保留 `ProjectScope` 防御性拦截。 |
| **D12 / D22 / D36 / C5** | **不改当前进程环境 + 按需统一重启提示 + 幂等短路** | 绝不调用 `setenv`/`_putenv`。当所有目标持久化值已等于新值时，**不备份、不写盘、不广播、不打印重启提示**，返回 `Env_Unchanged`；仅当实际发生写入/删除（`Env_OK`）时，在单次 API 调用末尾自动输出 1 次中英双语"请重启终端使环境变量生效"提示。 |
| **D13 / D14 / D29 / Q9** | **`chsrc_view_env` 精确匹配读持久化值，不确定时拒绝执行** | 保持 `void chsrc_view_env (const char *var1, ...)` 变参签名，不做批量 `gets`。按精确匹配定位持久化来源：能唯一确认值时优先打印 `NAME=persisted_value`（未设置输出 `NAME=`），若当前进程 `getenv(NAME)` 同时存在且与之不同，用 `chsrc_note2` 提示临时值差异；若无法确认当前 shell、相关配置文件间存在不同值冲突或行含复杂动态语法，**放弃执行并报错告知具体原因与文件路径**。 |
| **D15 / Q7=B** | **`value == NULL` 即删除；单变量与多变量增量一致** | 不设独立 `unset` 函数，`chsrc_set_env (name, NULL)`（或 `chsrc_set_env_block` 中某项值为 `NULL`）即删除该变量。POSIX 与 Windows 均按变量名独立增量合并/删除，同一 dish 内即使分多次调用 `chsrc_set_env` 也不会互相抹除其他变量。 |
| **D16 / D30 / Q6** | **各目标官方引号规范 + 注入字符严防** | - bash/zsh/profile：`export NAME="value"`（拒绝含 `"`、`\`、`$`、`` ` ``、`\r`、`\n`）<br>- fish `conf.d/chsrc.fish`：`set -x NAME "value"`（不用 `-Ux`；拒绝字符同上）<br>- `/etc/environment`：`NAME=value`（无 `export`、无引号；额外拒绝空白符、`#`、`'`）<br>- Windows 注册表：原值存入 `REG_SZ`（不加引号；拒绝 `\r`、`\n`） |
| **D17 / D32** | **Windows 注册表备份至 `%TEMP%\chsrc-<PID>\` 的 `.reg`** | 每次进程运行期间使用进程级临时子目录 `%TEMP%\chsrc-<PID>\`，写/删注册表前将旧状态导出为标准 UTF-16LE + BOM 格式的 `.reg` 文件（覆盖 HKCU 与 HKLM）；写入中途出错自动回滚并报错；`.reg` 创建失败须明确告知磁盘/权限/路径原因并中止。Dry-run 模式下不写注册表也不生成 `.reg`。 |
| **D19** | **模块不耦合 `reset` 模式** | `env.c` 不读 `chsrc_in_reset_mode ()`；`resetsrc` 设为上游源还是传 `NULL` 删除由各 recipe 自行决定。 |
| **D34** | **Windows 广播 `WM_SETTINGCHANGE`** | 注册表实际变更（`Env_OK`）后调用 `SendMessageTimeoutW (HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 1500, NULL)`，超时或失败静默忽略。 |
| **Q3=A** | **内部返回 `EnvOp_t` 可单测，公开 API 遇错即报错退出** | 底层内部实现完整返回 `EnvOp_t` 全部状态供 `test/fw.c` 覆盖测试；公开的 `chsrc_set_env` / `chsrc_set_env_block` 在非 `Env_OK`/`Env_Unchanged` 时统一打印双语错误并 `exit`，防止 recipe 漏检返回值导致打印假成功结论。 |
| **Q10 / Q11 / C5** | **模块完成后统一迁移 5 个 recipe，清除旧逻辑与双斜杠** | P3 阶段一次性迁移 `rustup`、`Pub`、`Flutter`、`nvm`、`Homebrew`：统一开放 `UserScope` 与 `SystemScope`（`ProjectScope` 为 `Unable`）；`Homebrew` 的 `getsrc` 迁入 `chsrc_view_env`，删除 `Homebrew/rawstr4c.{h,md}`，用 `hp_remove_trailing_slash` 消除 URL 双斜杠；Windows 下拦截不支持的 `Homebrew` 与 `nvm`；更新 `nvm` 的 Fish 备注并开启 `rustup` 英文支持。 |

---

## 3. 模块详细设计

### 3.1 API 设计（`src/framework/env.c`）

```c
typedef enum {
  Env_OK,                /* 实际发生写入或删除 */
  Env_Unchanged,         /* 所有目标持久化值已与期望一致，未备份、未写盘 */
  Env_NoPermission,      /* SystemScope 权限不足，未做任何修改 */
  Env_TargetUnwritable,  /* 目标文件/目录/注册表键不可写或备份创建失败 */
  Env_BadName,           /* 变量名不匹配 ^[A-Za-z_][A-Za-z0-9_]*$ */
  Env_BadValue,          /* 值含目标方言危险字符（引号/$/`/换行等） */
  Env_AmbiguousTarget,   /* 未知 shell、多文件值冲突或现有行含复杂动态语法 */
  Env_ScopeUnsupported   /* ProjectScope 等不支持的作用域（防御性） */
} EnvOp_t;

/* 读取当前进程环境变量（getenv 薄封装，未设置返回 NULL，return caller-free） */
char   *chsrc_get_env       (const char *name);

/* 单变量写入/删除（value == NULL 表示删除）；出错内部打印双语错误并 exit */
EnvOp_t chsrc_set_env       (const char *name, const char *value);

/* 多变量原子写入/删除：以首个 NULL name 结束的 (name, value) 交替参数表；
   单次备份、单次落盘、单次广播、最多输出 1 次重启终端提示 */
EnvOp_t chsrc_set_env_block (const char *name1, ...);

/* 查看持久化与当前环境变量（原 OS.c:642 迁入并重做，保持 NULL 结尾变参） */
void    chsrc_view_env      (const char *var1, ...);
```

### 3.2 平台 × Scope × 目标映射

| Scope | Windows（`#ifdef XY_Build_On_Windows`） | POSIX |
|---|---|---|
| `UserScope` | `HKCU\Environment` | 当前 shell 对应的写入集（见下表） |
| `SystemScope` | `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment` | `/etc/environment` |
| `ProjectScope` | 不支持（`ScopeCap_Unable`） | 不支持（`ScopeCap_Unable`） |
| `ImplementationDefinedScope` | 解析为 `UserScope` | 解析为 `UserScope` |

POSIX `UserScope` 写入集与检查集：

| 检测到的当前 shell | 写入集 | 检查集（发现同名变量仅 `chsrc_note2` 提醒） |
|---|---|---|
| `bash` | `~/.bashrc`（存在才写）、`~/.bash_profile`（存在才写）、`~/.profile`（不存在则创建） | `~/.zshrc`、`~/.zprofile`、`~/.config/fish/conf.d/chsrc.fish`、`~/.config/fish/config.fish` |
| `zsh` | `~/.zshrc`（存在才写）、`~/.zprofile`（存在才写）、`~/.profile`（不存在则创建） | `~/.bashrc`、`~/.bash_profile`、`~/.config/fish/conf.d/chsrc.fish`、`~/.config/fish/config.fish` |
| `fish` | `~/.config/fish/conf.d/chsrc.fish`（不存在则创建）<br>+ 一次性精确清理 `~/.config/fish/config.fish` 中 5 个迁移 dish 的旧同名行 | `~/.bashrc`、`~/.zshrc`、`~/.profile`（及非 5 dish 变量在 `config.fish` 中的同名定义） |
| 未知（`dash`/`tcsh`/`nu` 等） | **拒绝执行（`Env_AmbiguousTarget`）**，告知无法确认当前 shell 并给出手工添加指引 | — |

### 3.3 POSIX 行级精确匹配、复杂行保护与改写规则

1. **标识符边界与行语法识别**：逐行扫描时跳过 `#` 开头的注释行；严格校验变量名边界，杜绝 `ABC` 误命中 `ABCD`：
   - **sh / bash / zsh**：识别 `^[ \t]*(export[ \t]+)?NAME=(.*)$`（写出格式固定为 `export NAME="value"`）：
     - 若同行存在多个赋值（如 `export A=1 B=2`）、未闭合引号、命令替换/变量展开（含 `$` 或 `` ` ``）或非注释尾部管道/分号链接命令，判定为**复杂动态行（`Env_AmbiguousTarget`）**，拒绝自动覆写并报错指明文件与行号；
     - 允许安全解析并覆写标准双引号 `"..."`、单引号 `'...'` 或无空白无展开裸值（如历史 `nvm` 写出的 `export NVM_NODEJS_ORG_MIRROR=https://...`），行尾允许紧跟 `# ...` 纯注释。
   - **fish**：识别 `^[ \t]*set([ \t]+-[A-Za-z]+)*[ \t]+NAME([ \t]+.*)?$`（写出格式固定为 `set -x NAME "value"`）：
     - 仅当选项为导出语义（`-x` / `-gx` / `-xg` / `-g -x` / `-x -g`，不含 `-U` universal 或 `-e` erase 等冲突标志）且值部分为单个安全字面量（`"..."`、` '...'` 或无空白无 `$` 裸值）时视为安全可管行；否则判定为 `Env_AmbiguousTarget` 报错指明文件与行号。
   - **`/etc/environment`**：识别 `^[ \t]*(export[ \t]+)?NAME=(.*)$`（写出格式固定为无 `export`、无引号的 `NAME=value`）；含 `$`、`` ` ``、未配对引号或同行多词时判定为 `Env_AmbiguousTarget`。
2. **改写 / 重复行折叠 / 追加 / 删除**：
   - 文件已含 `NAME` 安全可管行 → 第 1 处原位替换为标准新行，同文件后续重复的 `NAME` 赋值行直接剔除；
   - 文件不含 `NAME` 且 `value != NULL` → 若原非空文件不以 `\n` 结尾先补 `\n`，再追加标准新行；
   - `value == NULL` → 删除该文件内所有匹配 `NAME` 的安全可管行。
3. **单文件级幂等与备份**：逐文件对比修改前后全文内容，**仅对内容实际变化的文件**执行 `chsrc_backup ()` 与写入；所有目标文件均无变化时返回 `Env_Unchanged`。

### 3.4 Windows 注册表与 `.reg` 备份回滚细则

1. **进程级备份目录**：首次写/删注册表前，在 `GetTempPathW` 下定位 `chsrc-<GetCurrentProcessId()>` 子目录（不存在则创建），按操作序号生成 `env-hkcu-<seq>.reg` 或 `env-hklm-<seq>.reg`（combo dish 同一根键去重只备份一次原始状态）。
2. **`.reg` 文件格式**：写入 UTF-16LE BOM（`0xFF 0xFE`）及 `Windows Registry Editor Version 5.00\r\n\r\n[<KeyPath>]\r\n`：
   - 原值存在（`REG_SZ`）→ 按 `.reg` 转义规则（`\` → `\\`，`"` → `\"`）写出 `"NAME"="old_value"`；
   - 原值存在（`REG_EXPAND_SZ` 等其他类型）→ 写出 `"NAME"=hex(2):...` 精确字节表示；
   - 原值不存在 → 写出 `"NAME"=-`。
3. **原子回滚**：多变量逐个调用 `RegSetValueExW` / `RegDeleteValueW` 过程中一旦某一步失败，立即将内存保存的各变量原状态写回注册表恢复现场，同时输出 `.reg` 备份路径供核查，再报错退出。

---

## 4. 实施阶段与 Commit 切分（单 PR 线性提交）

### Phase P0：前置基础设施修复（独立先行 commits）
1. `修复 chsrc_ensure_root 的 $EUID 与误判 root 缺陷并新增 chsrc_is_root`（**F1**）
2. `修复文件写入族函数空串写入失败、句柄泄漏与双语提示`（**F2**）
3. `改进 chsrc_backup 路径规范化与空格引号保护`（**F3**）
4. `统一 XY_Build_On_Windows 与 xy.on_windows 的平台判定与用法`（**F4**，含 `lib/xy.h` 与 `OS.c`）
5. `修复 CLI -scope 缺参崩溃与 --version 选项解析缺陷`（**F7**）
6. `补齐 reset 作用域校验并引入 ScopeCap_Able_But_Just_Docs 枚举`（**F5 + D10/D28**）
7. `显式后置链接 Windows 导入库并排除 Android 交叉编译`（**F6**）

### Phase P1 & P2：环境变量核心模块（`src/framework/env.c`）
8. `新增 framework/env.c 实现 POSIX 环境变量精确读写与持久化查看`（**P1**）
9. `实现 env.c 的 Windows 注册表后端、.reg 备份回滚与设置变更广播`（**P2**）

### Phase P3：统一迁移 5 个 Recipe（清除旧逻辑）
10. `迁移 rustup、Pub、Flutter、nvm、Homebrew 至 env 模块并移除旧逻辑`（**P3**）

### Phase P4：单元测试、CLI 回归测试与文档同步
11. `补充 env 模块单元测试、CLI 幂等回归测试与开发文档`（**P4**）
