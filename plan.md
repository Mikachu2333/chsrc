# 环境变量管理模块化 — 实施计划（修订版）

> 状态：**第一轮评审已合入，剩余 5 项联动冲突待最终确认（见第 4 节 C1–C5）**
> 分支：`arena/3e777a46-chsrc`（基于 `feat/env`）
> 范围：前置基础设施修复 + 新增 framework 层环境变量模块 + 统一迁移 5 个现有 env recipe
> 原则：未列入本计划的事一律按"不做"处理，不预留扩展点

---

## 0. 目标 / 非目标

### 目标

1. 把散落在 recipe 里的环境变量读写收敛成 `src/framework/env.c` 一处可复用实现，recipe 贡献者不再手写平台与 shell 方言分支。
2. 修掉实际缺陷：现有 5 个 env recipe（`rustup`、`Pub`、`Flutter`、`nvm`、`Homebrew`）全部只 `chsrc_append_to_file`，重复 `chsrc set` 无限堆积脏行。
3. 关掉命令注入（RCE）入口：
   - Windows 侧废除 `setx NAME "<url>"` 字符串拼接，改用 Win32 注册表宽字符 API，引号注入面归零；
   - POSIX 侧严格拦截双引号内仍会被 bash/zsh/fish 求值的 `$`、`` ` ``、引号、反斜杠与换行符。
4. 让 `chsrc get`（`chsrc_view_env`）精确匹配并优先报出**持久化值**，同时对比当前终端继承的临时值给出明确提示；多文件冲突无法唯一定位时拒绝盲猜并告知原因。
5. 在开始新模块实现前，以独立前置 commit 修掉框架中已识别的底层函数缺陷与平台宏混用问题（见第 1 节）。

### 非目标

- 不做 `PATH` 式"列表追加"语义（当前 0 个 recipe 需要）。
- 不做项目级（`ProjectScope`）环境变量的实际落地。
- 不在 POSIX 配置文件中写入 `# Ver2 CHSRC BLOCK ...` 注释包裹头尾，也不主动清扫历史遗留的 `# ------ chsrc BLOCK BEGIN/ENDIN ------` 等注释痕迹（仅对 `config.fish` 中阻塞生效的旧变量赋值行做一次性精确清理）。
- 不动 `Arch-Linux.c` 那类非环境变量的"配置文件换源"。

---

## 1. 需提前修复的既有错误 / 不完善函数清单（Phase P0）

以下既有问题直接影响本模块的正确性、安全性或跨平台一致性，须在编写 `env.c` 之前按职责拆分为独立前置 commit 先行修复：

| 序号 | 位置 | 既有问题 / 缺陷现象 | 前置修复方案 |
|---|---|---|---|
| **F1** | `src/framework/OS.c:280-299`<br>`chsrc_ensure_root ()` | 1. `getenv ("$EUID")` 多写了 `$`，恒为 `NULL`，导致每次必 fork `id -u`。<br>2. **严重提权误判漏洞**：`xy_str2int (buf)` 在 `buf == NULL` 或非数字时返回 `0`（见 `lib/xy.h:505`），一旦 `id -u` 执行失败（如 Windows 或精简容器环境），`0 != xy_str2int(buf)` 为假，**直接误判当前用户是 root**！<br>3. 缺少非中止的权限探测函数，且 Windows 无管理员组判定与对应提权文案。 | 1. 新增 `bool chsrc_is_root (void)`：POSIX 下直接调 `<unistd.h>` 的 `geteuid () == 0`（零 fork、无字符串解析歧义）；`#ifdef XY_Build_On_Windows` 下用 `AllocateAndInitializeSid` + `CheckTokenMembership` 检测 `DOMAIN_ALIAS_RID_ADMINS`。<br>2. `chsrc_ensure_root ()` 改为调用 `chsrc_is_root ()`，失败时按平台输出双语指引（Windows 提示以管理员身份运行终端，POSIX 提示 `sudo`/切换 `root`）并 `exit (Exit_UserCause)`。 |
| **F2** | `src/framework/OS.c:748-868`<br>`chsrc_overwrite_file ()`<br>`chsrc_append_to_file ()`<br>`chsrc_prepend_to_file ()` | 1. **空串写入必崩**：`chsrc_overwrite_file` 与 `chsrc_append_to_file` 执行 `fwrite (str, len, 1, f)` 后断言 `ret == 1`；按 C11 标准，当 `len == 0`（如删掉文件中唯一一条环境变量后覆写空串）时 `fwrite` 返回 `0`，误触发"写入文件失败"并 `exit (Exit_UserCause)`。<br>2. `chsrc_append_to_file` 在 `fwrite` 失败分支漏掉了 `fclose (f)`。<br>3. `chsrc_prepend_to_file` 未调用 `chsrc_ensure_dir`、未校验 `fwrite` 返回值、报错硬编码中文 `"文件打开失败"`。 | 单独一个 commit 改进文件写入族函数：`len > 0` 时才执行 `fwrite` 与 `ret != 1` 校验（`len == 0` 时打开 `"w"` 已完成截断清空）；补齐 `fclose (f)`、`chsrc_ensure_dir` 及中英双语错误信息。 |
| **F3** | `src/framework/OS.c:873-948`<br>`chsrc_backup ()` | 1. 未对入参 `path` 先做 `xy_normalize_path`：`ComboBackedUpPaths` 去重栈直接比较原始字符串（`"~/.bashrc"` 与展开后绝对路径可能不一致）；Windows 分支 `copy /Y <path> <path>.bak` 遇到 `~` 或正斜杠 `/` 会直接失败；路径含空格时 shell 命令未加引号保护。 | 对 `path` 先通过 `xy_normalize_path` 规范化用于去重与实际文件拷贝（日志仍展示直观路径），并在 shell 拷贝命令中对路径加引号（或直接用 C 标准 I/O 拷贝避免 fork 与注入）。 |
| **F4** | `src/framework/OS.c:407,495,957,1012`<br>`lib/xy.h:111,1405,1553`<br>`src/recipe/lang/Rust/rustup.c:66` | **`XY_Build_On_Windows` 与 `xy.on_windows` 用法混乱且存在逻辑断层**：<br>1. `OS.c:957,1012` 写了 `#if XY_Build_On_Windows`（非 Windows 下宏未定义），而 `OS.c:407` 写 `#ifdef XY_Build_On_Windows`。<br>2. `OS.c:495` 在 `#ifdef XY_Build_On_Windows` 块外用运行期 `if (xy.on_windows && !tmpfilepath)` 触发 `xy_unreached()`。<br>3. `lib/xy.h:1553` `_xy_detect_os()` 通过探测 `%SystemDrive%\Windows` 在运行期置 `xy.on_windows = true`；在 Cygwin/MSYS POSIX 编译器下 `_WIN32` 未定义（无 `XY_Build_On_Windows`），却会导致 `xy.on_windows == true`，进而使 `lib/xy.h:1405` `xy_dir_exist()` 落入空 `#ifdef` 分支**恒返回 `false`**。<br>4. `rustup.c:66` 用 `#ifdef XY_Build_On_Windows`，其余所有 recipe 用 `if (xy.on_windows)`。 | 建立全仓统一规范并修复（是否允许同步改 `lib/xy.h` 见 C2）：<br>- **编译期 `#ifdef XY_Build_On_Windows`**：一律用 `#ifdef`（禁用 `#if`），且**仅用于包裹 Win32 SDK 专有类型与 API 调用**（如 `HKEY`、`DWORD`、`RegOpenKeyExW`、`GetTempPathA`、`SYSTEM_INFO`）。<br>- **运行期 `if (xy.on_windows)`**：所有纯 C 跨平台分支逻辑一律用 `if (xy.on_windows)`，保证各平台编译器始终对全部分支做语法/类型检查。 |
| **F5** | `src/framework/waiter.c:882`<br>`src/framework/source.c:797`<br>`src/framework/waiter.c:250` | 1. `waiter_handle_Reset_Source ()` 漏调了 `chsrc_check_scope_capability (dish)`，导致 `chsrc reset -scope=project <dish>` 绕过框架拦截。<br>2. `chsrc_check_scope_capability ()` 与 `cli_print_dish_features ()` 中的 Scope 状态提示硬编码中文，不支持 `-en`。 | 在 `waiter_handle_Reset_Source ()` 非 combo 分支补齐 `chsrc_check_scope_capability (dish)`；补齐 Scope 检查与 `ls` 展示的英文文案。 |
| **F6** | `Makefile:61-67,168-170,240,245`<br>`justfile:59-88` | 1. 现有 `OS.c` / `xy.h` 已调用 `kernel32` / `shell32` API，但构建脚本完全依赖编译器隐式链接。<br>2. `Makefile` 在 Windows MSYS2 上交叉编译 Android（`CROSS_BUILD_WINDOWS_FOR_ANDROID=1`）时 `On-Windows` 仍为 `1`，若无条件追加 Windows 导入库会打挂 Android Release CI。<br>3. MinGW 单遍链接器要求 `-l*` 位于源文件与 `-static` 之后。 | 在 `Makefile`（加 `ifneq ($(CROSS_BUILD_WINDOWS_FOR_ANDROID), 1)` 守卫）与 `justfile` 中新增尾部链接库变量 `LDLIBS_win = -ladvapi32 -lkernel32 -luser32 -lshell32`，统一置于编译命令行末端（`.c` 文件之后）。 |
| **F7** | `src/chsrc-main.c:171,231`<br>（待 C4 确认是否纳入） | 1. `chsrc set -scope` 末尾缺参时 `argv[i+1]` 为 `NULL`，传入 `xy_strcat` 导致空指针解引用崩溃；且消费 `argv[i+1]` 后未执行 `i++`。<br>2. 第 231–232 行判断 `-version`/`--version` 误写 `xy_streql (command, ...)` 而非 `argv[i]`，导致 `chsrc -en --version` 报未知选项。 | 若确认纳入前置修复，单独小 commit 补上 `i + 1 <= argc` 边界检查、`i++` 及 `argv[i]` 修正。 |

---

## 2. 已锁定决策汇总

| 编号 | 决策结论 | 工程落地细则 |
|---|---|---|
| **架构与写入模型** | **无注释头、按变量名精确行编辑**（由 Q7=B、Q8、D26、D35 修订原 D1/D5/D6/D35） | POSIX 配置文件不再写入 `# Ver2 CHSRC BLOCK OP/ED` 或 `# src=...` 等注释头尾；直接按目标方言匹配同名环境变量赋值行，已存在则原位改写，不存在则追加，值为 `NULL` 则删除对应赋值行。文件中用户原有注释、其他配置以及历史 `# ------ chsrc BLOCK BEGIN/ENDIN ------` 注释痕迹一律保留不碰（`block.c` 是否取消见 C1）。 |
| **D2 / D31 / Q5** | **Win32 注册表宽字符 API + `REG_SZ`** | 使用全版本通用的 `RegOpenKeyExW` / `RegQueryValueExW` / `RegSetValueExW` / `RegDeleteValueW` / `RegCloseKey`（避免 `RegGetValueW` 的 `_WIN32_WINNT >= 0x0600` 门槛问题）；写入类型固定为 `REG_SZ`（读取时兼容既有 `REG_SZ` 与 `REG_EXPAND_SZ`），在 `env.c` 内提供 `static` UTF-8 ↔ UTF-16LE 转换（`MultiByteToWideChar` / `WideCharToMultiByte`）。 |
| **D3 / Q1** | **Windows 导入库全部显式后置链接** | `Makefile`（排除 `CROSS_BUILD_WINDOWS_FOR_ANDROID=1`）与 `justfile` 显式后置链接 `-ladvapi32 -lkernel32 -luser32 -lshell32`。 |
| **D4 / D23 / D24** | **POSIX 按当前 shell 分派写入集与检查集** | 检测优先级：父进程 `comm`（Linux/Android 读 `/proc/<ppid>/comm`，macOS/BSD 走 `ps -p <ppid> -o comm=`）→ `$SHELL` → 回退（与 Q9 的关系见 C3）。<br>- **写入规则**：已知 rc 文件仅在已存在时写入；**`~/.profile` 例外**（不存在则创建兜底登录环境）；当前 shell 为 **fish** 时目标为 `~/.config/fish/conf.d/chsrc.fish`（不存在则创建）。<br>- **检查集提醒**：其余已知 shell 配置文件若检测到同名变量，用 `chsrc_note2` 双语提示，不跨 shell 清理。 |
| **D25** | **fish `config.fish` 加载顺序陷阱一次性清理** | fish 先加载 `conf.d/*.fish` 后加载 `config.fish`。对迁移的 5 个 dish 涉及的已知变量名，写入 `conf.d/chsrc.fish` 时若检测到 `~/.config/fish/config.fish` 中存在非注释的 `set -x` / `set -gx` 同名行，先 `chsrc_backup` 再精确删除该行。 |
| **D8 / D18 / D33** | **开放 `SystemScope` + 前置权限自检** | POSIX 写 `/etc/environment`，Windows 写 `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment`。执行前先调 `chsrc_is_root ()` 自检权限；不足则不做任何修改、不建备份，直接打印对应平台的提权/手工修改双语指引并 `exit (Exit_UserCause)`。 |
| **D9** | **`ImplementationDefinedScope` → `UserScope`** | 在 env 模块内默认作用域固定解析为 `UserScope`。 |
| **D10 / D28 / Q4** | **新增 `ScopeCap_Able_But_Just_Docs`，本分支暂置空** | 在框架层完成枚举定义、`ls` 黄色 `HalfYesMark` 双语展示及 `chsrc_check_scope_capability` 放行支持（`chef_set_default_scope` 联动见 C4）；本分支不修改具体 dish 使用该枚举（留给后续如 Hugging Face 等纯文档 dish）。`waiter_handle_Reset_Source` 补齐 scope 校验，`env.c` 内部保留 `ProjectScope` 防御性拦截。 |
| **D12 / D22 / D36** | **不改当前进程环境 + 按需统一重启提示 + 幂等短路** | 绝不调用 `setenv`/`_putenv`。当所有目标持久化值已等于新值时，**不备份、不写盘、不广播、不打印重启提示**，返回 `Env_Unchanged`；仅当实际发生写入/删除（`Env_OK`）时，在单次 API 调用末尾自动输出 1 次中英双语"请重启终端使环境变量生效"提示（删除 recipe 中手写的重复提示）。 |
| **D13 / D14 / D29 / Q9** | **`chsrc_view_env` 精确匹配读持久化值，不确定时拒绝执行** | 保持 `void chsrc_view_env (const char *var1, ...)` 变参签名，不做批量 `gets`。按精确匹配定位持久化来源：能唯一确认值时优先打印 `NAME=persisted_value`，若当前进程 `getenv(NAME)` 同时存在且与之不同，用 `chsrc_note2` 提示临时值差异；若相关配置文件间存在冲突值或语法无法精确解析，**放弃执行并报错告知具体原因与文件路径**。 |
| **D15 / Q7=B** | **`value == NULL` 即删除；单变量与多变量增量一致** | 不设独立 `unset` 函数，`chsrc_set_env (name, NULL)`（或 `chsrc_set_env_block` 中某项值为 `NULL`）即删除该变量。POSIX 与 Windows 均按变量名独立增量合并/删除，同一 dish 内即使分多次调用 `chsrc_set_env` 也不会互相抹除其他变量。 |
| **D16 / D30 / Q6** | **各目标官方引号规范 + 注入字符严防** | - bash/zsh/profile：`export NAME="value"`（拒绝含 `"`、`\`、`$`、`` ` ``、`\r`、`\n`）<br>- fish `conf.d/chsrc.fish`：`set -x NAME "value"`（不用 `-Ux`；拒绝字符同上）<br>- `/etc/environment`：`NAME=value`（无 `export`、无引号；额外拒绝空白符、`#`、`'`）<br>- Windows 注册表：原值存入 `REG_SZ`（不加引号；拒绝 `\r`、`\n`） |
| **D17 / D32** | **Windows 注册表备份至 `%TEMP%\chsrc-<PID>\` 的 `.reg`** | 每次进程运行期间生成唯一临时子目录 `%TEMP%\chsrc-<PID>\`（或进程生命周期内固定的运行时 ID），写/删注册表前将旧状态导出为标准 UTF-16LE + BOM 格式的 `.reg` 文件（覆盖 HKCU 与 HKLM）；写入中途出错自动回滚并报错；`.reg` 创建失败须明确告知磁盘/权限/路径原因并中止。Dry-run 模式下不写注册表也不生成 `.reg`。 |
| **D19** | **模块不耦合 `reset` 模式** | `env.c` 不读 `chsrc_in_reset_mode ()`；`resetsrc` 设为上游源还是传 `NULL` 删除由各 recipe 自行决定。 |
| **D34** | **Windows 广播 `WM_SETTINGCHANGE`** | 注册表实际变更（`Env_OK`）后调用 `SendMessageTimeoutW (HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 1500, NULL)`，超时或失败静默忽略。 |
| **Q3=A** | **内部返回 `EnvOp_t` 可单测，公开 API 遇错即报错退出** | 底层内部实现完整返回 `EnvOp_t` 全部状态供 `test/fw.c` 覆盖测试；公开的 `chsrc_set_env` / `chsrc_set_env_block` 在非 `Env_OK`/`Env_Unchanged` 时统一打印双语错误并 `exit`，防止 recipe 漏检返回值导致打印假成功结论。 |
| **Q10 / Q11** | **模块彻底完成后统一迁移 5 个 recipe，清除全部旧逻辑** | P4 阶段一次性迁移 `rustup`、`Pub`、`Flutter`、`nvm`、`Homebrew`：统一开放 `UserScope` 与 `SystemScope`（`ProjectScope` 为 `Unable`）；`Homebrew` 的 `getsrc` 迁入 `chsrc_view_env`，移除 `Homebrew/rawstr4c.{h,md}` 旧模板并修正 URL 双斜杠，非支持平台（Windows 下的 `Homebrew` 与 `nvm`）明确报错拦截；`nvm` 更新 Fish 相关说明。 |

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
  Env_AmbiguousTarget,   /* 目标文件/语法存在歧义或冲突，拒绝盲目改写 */
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

> 注：`env.c` 内部提供不调用 `exit()` 的 `static EnvOp_t env_apply_pairs (...)` 等核心函数，供 `test/fw.c` 直接断言每种 `EnvOp_t` 错误码。

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
| 未知（`dash`/`tcsh`/`nu` 等） | 待 C3 最终确认（按 D23 回退写所有已存在 rc + `~/.profile`，或按 Q9 拒绝执行并提示） | 其余已知配置文件 |

### 3.3 POSIX 行级匹配与改写规则

不含任何 `# Ver2 CHSRC BLOCK` 注释包裹：
1. **标识符边界匹配**：逐行扫描时跳过 `#` 开头的注释行；严格校验变量名右边界为 `=`（bash/zsh/environment）或空白符（fish），杜绝 `ABC` 误匹配 `ABCD`。
   - bash / zsh / profile 匹配形态：`^[ \t]*export[ \t]+NAME=(.*)$`
   - fish 匹配形态：`^[ \t]*set[ \t]+(-x|-gx|-g[ \t]+-x|-x[ \t]+-g)[ \t]+NAME([ \t]+.*)?$`
   - `/etc/environment` 匹配形态：`^[ \t]*(export[ \t]+)?NAME=(.*)$`（写出时统一规范为无 `export`、无引号的 `NAME=value`）
2. **改写 / 追加 / 删除**：
   - 文件已含 `NAME` 匹配行 → 将首个匹配行原位替换为标准格式新行，若同文件内存在历史重复堆积的同 `NAME` 赋值行则一并折叠清理（详见 C1 确认）；
   - 文件不含 `NAME` 匹配行且 `value != NULL` → 若原非空文件末尾缺 `\n` 先补 `\n`，再追加标准格式新行；
   - `value == NULL` → 删除该文件中匹配 `NAME` 的赋值行。
3. **单文件级幂等与备份**：逐文件对比修改前后全文内容，**仅对内容确实发生改变的文件**调用 `chsrc_backup ()` 与覆写；全部目标文件内容均未改变时返回 `Env_Unchanged`。

### 3.4 Windows 注册表与 `.reg` 备份回滚细则

1. **进程级备份目录**：首次写/删注册表前，在 `GetTempPathW` 下定位 `chsrc-<GetCurrentProcessId()>` 子目录（不存在则创建），生成备份文件（如 `env-hkcu-001.reg` / `env-hklm-001.reg`）。
2. **`.reg` 文件内容**：写入 UTF-16LE BOM（`0xFF 0xFE`）与 `Windows Registry Editor Version 5.00\r\n\r\n[<KeyPath>]\r\n`：
   - 对本次即将修改/删除的每个变量：若原值存在，按 `.reg` 规范转义写出 `"NAME"="old_value"`（若原类型为 `REG_EXPAND_SZ` 则写 `hex(2):...`）；若原值不存在，写出 `"NAME"=-`（以便回滚或用户双击导入时删除新增项）。
3. **原子回滚**：多变量逐个调用 `RegSetValueExW` / `RegDeleteValueW` 过程中一旦某一步失败，立即将内存保存的各变量原状态写回注册表恢复现场，同时输出 `.reg` 备份路径供核查，再报错退出。

---

## 4. 仍需最终确认的 5 项冲突 / 边界点（C1–C5）

在合入上一轮决策时，发现以下几项回答之间存在联动冲突或边界待敲定：

1. **C1 — "放弃注释头（Q8/D35）"与原 `block.c`（D1）/ `CurrentDish`（D27）的去留 + 历史重复行处理**
   - 既然 Q8 与 D35 已决定**放弃写 block 注释头尾**，改为直接按变量名追加/改写（且 Q7=B 按变量名增量合并）：
     - ① 是否**直接取消 `src/framework/block.c` 以及 `ProgStatus.CurrentDish` / `CurrentSource`（原 D1、D27）**，将行级键值编辑作为 `static` 函数内聚在 `src/framework/env.c` 中？（推荐：**是**，少一层无用的空壳抽象）
     - ② 当同一配置文件内存在历史 `chsrc set` 多次追加留下的**多条同名变量赋值行**时，改写时是否保留第 1 条位置原位更新、并删除后面多余的同名赋值行（注释行不动）？（推荐：**是**，否则旧的末尾脏行在 shell 中仍会覆盖前面的改写行！）
     - ③ 若已有同名赋值行包含动态展开（如 `export FOO="$OTHER/bar"`），是直接覆写，还是按 Q9 视为不确定而报错退出？（推荐：**报错退出并告知文件与行号**，避免误毁用户自定义脚本逻辑）

2. **C2 — 统一 `xy.on_windows` 与 `XY_Build_On_Windows`（Q5）是否允许修改 `lib/xy.h`**
   - 原计划第 0 节写有"不改 `lib/xy.h`"，但 `lib/xy.h:1405`（`xy_dir_exist`）与 `lib/xy.h:1553`（`_xy_detect_os`）正是二者语义分裂的源头（Cygwin/MSYS 下无 `XY_Build_On_Windows` 却置 `xy.on_windows = true`，导致 `xy_dir_exist` 恒返回 `false`）。
   - 请确认：**允许在独立前置 commit 中同步修正 `lib/xy.h`**（让 `_xy_detect_os` 与 `XY_Build_On_Windows` 对齐，Cygwin/MSYS 归入 Unix），还是**严禁动 `lib/xy.h`、仅在 `src/` 内统一**？

3. **C3 — 未知 shell 时 `chsrc set` 的行为：遵循 D23 还是 Q9？**
   - `get`（`chsrc_view_env`）在无法唯一定位目标文件或值冲突时已明确按 Q9 **放弃执行并告知原因**。
   - 当 **`set`** 遇到父进程 `comm` 与 `$SHELL` 均无法识别当前 shell（如 `dash`/`nu`/容器无 `$SHELL`）时：
     - **A（按 D23 原推荐）**：回退写入所有已存在的已知 rc 文件 + `~/.profile`；
     - **B（按 Q9 统一精神）**：同样放弃执行并告知用户无法确认当前 shell 及手工配置方法。
     请确认选 **A** 还是 **B**？

4. **C4 — `chef_set_default_scope`（`chef.c:393`）放行确认 + `chsrc-main.c` CLI 小瑕疵（F7）是否纳入**
   - ① D28 引入 `ScopeCap_Able_But_Just_Docs` 时，是否同步修改 `src/framework/chef.c:393`，允许将标为 `ScopeCap_Able_But_Just_Docs` 的作用域设为 dish 的 `default_scope`（否则纯文档型 dish 设默认作用域会直接 `chsrc_breakdown` 崩溃）？（推荐：**同步放行**）
   - ② 第 1 节 **F7** 列出的 `chsrc-main.c:171,231` 两处 CLI 参数解析小 Bug（`-scope` 末尾缺参空指针崩溃、`-en --version` 误报未知选项），是否一并放入 P0 前置修复？

5. **C5 — 末尾笔误确认（D36） + `Homebrew` 双斜杠处理**
   - ① 你上一轮回复末尾写了两次 `D30按推荐`（紧跟在 D35 之后），此处按 **D36 按推荐**（`Env_Unchanged` 不备份、不写盘、不提示重启终端）理解是否正确？
   - ② `Homebrew` 迁移删除 `Homebrew/rawstr4c.{h,md}` 后，`Homebrew.c` 中镜像 URL 末尾自带 `/`（如 `"https://mirrors.tuna.tsinghua.edu.cn/"`），生成环境变量时将统一用 `hp_remove_trailing_slash` 去掉多余斜杠（产出规范的 `https://mirrors.tuna.tsinghua.edu.cn/git/homebrew/brew.git` 而非原先的 `//git/...`），是否同意？

---

## 5. 实施阶段与 Commit 切分（单 PR 线性提交）

### Phase P0：前置基础设施修复（独立先行 commits）
1. `fix(os): add chsrc_is_root() and fix $EUID / false-root bug in chsrc_ensure_root()`（对应 **F1**）
2. `fix(os): handle empty content and close file properly in chsrc_*_file() helpers`（对应 **F2**）
3. `fix(os): normalize and quote paths in chsrc_backup()`（对应 **F3**）
4. `refactor: unify XY_Build_On_Windows and xy.on_windows usage`（对应 **F4**，范围依 C2 定）
5. `fix(framework): check scope capability on reset and add ScopeCap_Able_But_Just_Docs`（对应 **F5 + D10/D28**，及可选 **F7**）
6. `build(windows): explicitly link Win32 import libs and guard Android cross-build`（对应 **F6**）

### Phase P1：POSIX 环境变量后端（`src/framework/env.c`）
7. 引入 `src/framework/env.c`（在 `chsrc-main.c` 中紧随 `framework/OS.c` 之后 `#include`）：实现标识符/危险字符校验、shell 检测、POSIX 行级精确匹配与原位改写/追加/删除、写入集/检查集分派、`config.fish` 5 dish 旧行精确清理、`SystemScope` `/etc/environment` 支持、幂等短路与统一双语重启提示，以及重做后的 `chsrc_view_env`（从 `OS.c` 迁入）。

### Phase P2：Windows 注册表后端（`src/framework/env.c`）
8. 在 `env.c` 中实现 `#ifdef XY_Build_On_Windows` 分支：UTF-8↔UTF-16 转换、`RegOpenKeyExW` 族 `HKCU`/`HKLM` 读写删、`%TEMP%\chsrc-<PID>\*.reg` UTF-16LE 导出备份与失败自动回滚、`WM_SETTINGCHANGE` 短超时广播、Windows 侧 `chsrc_view_env` 精确比对。

### Phase P3：统一迁移 5 个 Recipe（模块完成后集中切换，不留旧逻辑）
9. 迁移 `rustup.c`、`Dart/Pub.c`、`Dart/Flutter.c`、`JavaScript/nvm.c`、`ware/Homebrew/Homebrew.c`（删除 `Homebrew/rawstr4c.{h,md}`、修正双斜杠、补齐 Windows 平台拦截与 `SystemScope` 能力标记、移除各 recipe 手写的重启或手动清理提示）。

### Phase P4：单元测试、CLI 回归测试与文档同步
10. `test/fw.c`：纳入 `env.c`，覆盖名称/危险字符注入拦截、单/多变量原位改写与重复行折叠、删除清空、幂等短路、多文件冲突报错及 `HOME` 沙箱集成测试（Windows 下覆盖注册表读写删与 `.reg` 生成）。
11. `test/cli.pl`：新增幂等回归与 `get` 持久化回读断言。
12. 更新 `doc/10-如何编写recipe.md`、`.github/copilot-instructions.md`、`README.md`（Windows 手动编译显式链接示例）。
