# 环境变量管理模块化 — 实施计划

> 状态：**待最终确认**（第 3 节 14 项未决）
> 分支：`feat/env`（当前与 `dev` 同点，工作树干净）
> 范围：新增 framework 层环境变量模块 + 迁移 5 个现有 recipe
> 未列入本计划的事：一律按"不做"处理，不预留扩展点

---

## 0. 目标 / 非目标

**目标**

1. 把散落在 recipe 里的环境变量读写收敛成一处可复用实现，recipe 贡献者不再手写方言分支。
2. 修掉实际缺陷：现有 5 个 env recipe 全部只 `chsrc_append_to_file`，重复 `chsrc set` 无限堆积脏行。
   证据：`src/recipe/ware/Homebrew/Homebrew.c:28` 自己写着"若多次换源，请手动清理 profile 文件"。
3. 关掉一个注入口：`rustup`/`Pub`/`Flutter`/`nvm` 都 `chef_allow_user_define`，用户提供的 URL 被直接拼进
   `setx NAME "<url>"`（`src/recipe/lang/Dart/Pub.c:58`、`src/recipe/lang/Rust/rustup.c:68`），值中含 `"` 可拼出第二条命令。
4. 让 `getsrc` 能报出**持久化值**，而不是当前终端继承的临时值。

**非目标**

- 不做 PATH 式"列表追加"语义（当前 0 个 recipe 需要）。
- 不做项目级（ProjectScope）环境变量的实际落地。
- 不改 `lib/xy.h`（上游独立 MIT 库）；新增 helper 一律放 framework 内 `static`。
- 不动 `Arch-Linux.c` 那类"配置文件换源"（内核原语二期再服务它们，见 D1）。

---

## 1. 已锁定决策

| # | 决策 | 工程含义 |
|---|---|---|
| **D1** | **B：分两层** | 内核层 `block.c` 提供"按 tag 精确切块/替换/删除文件内容"原语；`env.c` 是其上的环境变量薄壳。一期只暴露 `chsrc_*_env*` 给 recipe，内核原语保持 `static` 且不进 recipe 可见 API（doc/10 第 96 行的前缀约定不新增）。 |
| **D2** | **B：Win32 注册表 API** | `RegGetValueW`/`RegSetKeyValueW`/`RegDeleteValueW`。不再有 `setx` shell-out，环境变量名由注册表值名全等匹配，`abc`/`abcd` 风险归零，引号注入面归零。 |
| **D3** | **全部显式链接** | Windows 构建入口一律显式列出导入库，不依赖隐式链接。清单见第 2.5 节。 |
| **D4** | **按当前 shell 写对应文件；fish 用 conf.d；写入集扩充到登录文件；其余文件冲突只提醒** | 写入集 = 当前 shell 的 rc + 登录 shell 文件；检查集 = 其余已知 rc，发现同名变量或旧 chsrc 痕迹时 `chsrc_note2` 提示、**不清理**。精确映射表见 2.2，检测手段见 D23。 |
| **D5** | **B：只替换 block** | 管理单元只有 chsrc block。文件里用户原有的注释、散行一律不动（含 `#begin` 之类）。同一文件内同 tag 出现多次 → 全部剔除并合并为一块（这是唯一需要"清扫散行"的场景，且清扫范围限于 chsrc 自己的 block）。 |
| **D6** | **Ver2 block 格式** | 起始 `# Ver2 CHSRC BLOCK OP of XXX`，第二行为特殊参数（源简写、system/user 等），结束 `# Ver2 CHSRC BLOCK ED`。**不识别 v1 的 `BEGIN`/`ENDIN`**。标记行文本细节见 D27/D35。 |
| **D7** | **A：tag 由 dish 别名决定** | recipe 不传 tag；模块内部取当前 dish 的首别名的。上下文来源见 D27。 |
| **D8** | **B：开放 SystemScope** | POSIX = `/etc/environment`；Windows = HKLM `SYSTEM\CurrentControlSet\Control\Session Manager\Environment`。执行前**先自检权限**，不足则不写任何东西，直接打印"如何提权/如何手工修改"的指引（`chsrc_error` + `exit(Exit_UserCause)`，与 `chsrc_ensure_root()` 现有行为一致）。 |
| **D9** | **ImplDefined → User** | `ImplementationDefinedScope` 在 env 模块里固定解释为 UserScope。 |
| **D10** | **ProjectScope 标 `Unable`，并新增枚举 `ScopeCap_Able_But_Just_Docs`** | 新枚举的判定语义/显示/首批使用对象未定，见 D28。 |
| **D12** | **不更新当前进程** | 模块绝不调用 `setenv`/`_putenv`。任何写入后一律提示重启终端（见 D22）。 |
| **D13** | **一期就实现"读持久化值"** | `chsrc_view_env` 迁进 `env.c` 并重做：同时读持久化值与 `getenv`，**持久化值优先展示**；两者都存在且不同 → 提示用户"当前终端里的是临时值，重启后以持久化值为准"。持久化值来源 = Windows 注册表 / POSIX 我们自己写的 block（因此可解析、可信）。API 形状见 D29。 |
| **D14** | **不做 `gets`** | 批量 get 的真实需求由 D13 的 `chsrc_view_env` 覆盖。 |
| **D15** | **不做 `clear`/`unset` 独立函数** | `set(name, NULL)` 即删除。 |
| **D16** | **引号与各目标官方推荐一致** | 分目标规则见 2.4；细则待 D30 确认（含 fish `set -x` 与 `set -Ux` 的选择）。 |
| **D17** | **备份到 temp 目录的 `.reg`** | 写注册表前把旧值落成 `<格式化本地时间>.reg`；出错时自动回滚并提示；`.reg` 创建失败必须说明原因。命名/目录/覆盖范围待 D32。 |
| **D18** | **新增 `chsrc_is_root()`** | 非中止判定，供 D8 的权限自检使用。`OS.c:282` 的 `getenv ("$EUID")` bug 是否搭本分支，见 D33。 |
| **D19** | **模块不碰 reset 语义** | 模块只是工具，不读 `chsrc_in_reset_mode()`。`reset` 是 `set(name, NULL)` 还是 `set(name, 官方源)` 由 recipe 作者自己写。 |
| **D20** | **沿用 `feat/env`，单 PR** | commit 切分见第 4 节。 |
| **D22** | **统一重启终端提示** | `chsrc_view_env` 之外，所有写入成功路径由模块自动输出中英双语提示；删除 recipe 中手写的 `chsrc_alert2 ("请重启终端...")`（`rustup.c:110`、`Homebrew.c:83`）。触发条件见 D36。 |

---

## 2. 模块设计

### 2.1 API（`src/framework/env.c`）

```c
typedef enum {
  Env_OK,                /* 实际发生写入 */
  Env_Unchanged,         /* 持久化值已等于新值，未写盘、未备份 */
  Env_NoPermission,      /* D8：权限不足，未做任何修改 */
  Env_TargetUnwritable,  /* 文件/键不可写、只读挂载、写失败 */
  Env_BadName,           /* 不匹配 ^[A-Za-z_][A-Za-z0-9_]*$ */
  Env_BadValue,          /* 含目标无法安全表达字符（按 2.4 规则判定） */
  Env_ScopeUnsupported   /* 防御性：正常路径由 chsrc_check_scope_capability 提前拦截 */
} EnvOp_t;

char   *chsrc_get_env       (const char *name);            /* getenv 薄封装；未设置返回 NULL；return caller-free */
EnvOp_t chsrc_set_env       (const char *name, const char *value);   /* value==NULL 即删除 */
EnvOp_t chsrc_set_env_block (const char *name1, ...);      /* NULL 结尾的 name,value 交替表；原子单块 */
void    chsrc_view_env      (const char *var1, ...);       /* 由 OS.c:641 迁入，按 D13 重做 */
```

scope 与 OS 都不作参数：scope 取自 `ProgMode.Scope`（`base.c:41`，配合 `chsrc_in_*_scope_mode()`，`base.c:61-63`），OS 取自 `xy.on_*`（`xy.h:81-85`，由 `_xy_detect_os()` 赋值）。
框架已在调用 `setfn` 前用 `chsrc_check_scope_capability()` 拦截过非法 scope（`waiter.c:870` → `source.c:797`），模块只读不校验。

### 2.2 平台 × scope × 目标

| scope | Windows | POSIX |
|---|---|---|
| User | `HKCU\Environment` | 当前 shell 的 rc + 登录文件（下表） |
| System | `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment` | `/etc/environment` |
| Project | 不支持（D10 `Unable`） | 不支持 |
| ImplDefined | → User | → User |

POSIX User 写入集 / 检查集（D4）：

| 检测到的 shell | 写入 | 只提醒不清理 |
|---|---|---|
| bash | `~/.bashrc` + `~/.bash_profile` + `~/.profile` | `~/.zshrc`、`~/.config/fish/**` |
| zsh | `~/.zshrc` + `~/.zprofile` + `~/.profile` | `~/.bashrc`、`~/.config/fish/**` |
| fish | `~/.config/fish/conf.d/<文件名待定>` | `~/.config/fish/config.fish`、`~/.bashrc`、`~/.zshrc` |
| 未知（dash/tcsh/nu…） | 回退策略待定（D23） | 全部 |

### 2.3 内核层（`src/framework/block.c`，一期全部 `static`）

```c
static char *chsrc_block_upsert (const char *content, const char *tag, const char *block, bool *existed);
static char *chsrc_block_remove (const char *content, const char *tag);
static bool  chsrc_block_has_var (const char *content, const char *name);   /* 供 D4 冲突提醒 */
```

单 TU 构建下 `static` 即可限制可见面（同 `OS.c:22` `log_for_file_write` 的做法）。
匹配规则：从 `# Ver2 CHSRC BLOCK OP of <tag>` 起，到**首个** `# Ver2 CHSRC BLOCK ED` 止；同 tag 多块 → 全部剔除、结果保留一块。
不含任何方言知识——`export` / `set -x` / `KEY=VALUE` 由 env 层生成正文后传入。

### 2.4 各目标的书写规则（D16，待 D30 逐条确认）

| 目标 | 一行形态 | 引号 |
|---|---|---|
| bash/zsh rc | `export NAME="value"` | 双引号 |
| fish conf.d | `set -x NAME "value"` | 双引号（`set -Ux` 与否待定） |
| `/etc/environment` | `NAME=value` | **不加**引号、不加 `export`（pam_env 语义） |
| 注册表 `REG_*` | 原值 | **不加**引号（加了会让 rustup 收到带引号的 URL） |

值校验：`Env_BadValue` 的判据按目标分别定义——rc 拒绝含 `"`/换行/`\`；`/etc/environment` 再拒绝含 `#`；注册表只拒绝换行（不经 shell，理论上无注入面）。

### 2.5 Windows 显式链接清单（D3）

需要的导入库：`-ladvapi32`（注册表）、`-lkernel32`（`GetTempPathA`/`GetTempFileNameA`，`OS.c:420` 已在用但目前靠隐式链接）、`-luser32`（仅当 D34 决定广播 `WM_SETTINGCHANGE`）。

落点（已核实的编译入口）：

- `Makefile`：在 `ifeq ($(On-Windows), 1)` 块（`Makefile:61-67`）内 `CFLAGS += $(CFLAGS_win_libs)`，自动覆盖 `Build-Command`(168)、`Build-Command-For-Release`(170)、`test-xy`(240)、`test-fw`(245)。
- `Justfile`：`CFLAGS_base`（`Justfile:59` 附近）加 `if os() == 'windows'` 分支，覆盖 dev(112)/debug(117)/release(127、129)。
- CI：`build-on-Windows.yml:42,52,63`、`build-on-Windows-Arm64.yml:43`、`PR-test.yml:41,45,68,72` 全部走 `make`/`just`，**无需改动 workflow**。
- `bootstrap/Termux.bash`（Android）、`tool/installer.sh:105,113`（POSIX）、`tool/installer.ps1`（无编译命令）均不涉及。
- `README.md:290` 的手动编译示例是 POSIX 命令，需另加一行 Windows 显式链接示例（进 P5 文档）。

### 2.6 备份与回滚

- POSIX：走现有 `chsrc_backup()`（`OS.c:873`，含 combo dish 去重栈 `ProgStatus.ComboBackedUpPaths`）；同一文件在一次命令里被多个 sub dish 改时，备份次数必须仍为 1。
- Windows：写前把将变动的键导出为 `.reg` 到 temp 目录（D17，细则待 D32）；写失败 → 用刚导出的内容回滚 + `chsrc_error2`；导出失败 → 明确说明原因（磁盘/权限/路径）。
- dry-run：`in_dry_run_mode()`（`base.c:71`）必须在注册表分支自行 gate（rc 分支若走 `chsrc_overwrite_file` 会继承 `OS.c:836` 的 gate）；dry-run 下既不写注册表也不生成 `.reg`（对齐既有 commit `4bed234` 的取向）。

---

## 3. 仍需你决策（14 项）

### D23 — 当前 shell 如何确认，失败怎么回退
模块需要"确认当前使用的是哪个 sh"，但 chsrc 是被 shell  fork 的子进程，能拿到的信号有限。
- A. `$SHELL`（登录 shell，未必等于当前正在用的那个；从 bash 里 `zsh` 再跑 chsrc 会误判）
- B. 父进程 comm：POSIX 下 `/proc/<ppid>/comm`，Linux/Android 可靠；macOS/BSD 无 procfs，需退回 `ps -p <ppid> -o comm=`
- C. B 优先、失败退 A、再失败退"全部已存在文件都写"
- **推荐 C**，且明确"未知 shell"时走 C 的最后一级回退（与现网行为最接近，不猜）。
- 附带：MSYS2/Cygwin 下按 D2 走注册表还是也走 shell 分支？推荐**仍走注册表**（`xy.on_windows` 为真即注册表，单一判据）。

### D24 — 文件不存在时是否创建 + conf.d 文件名
- `~/.bash_profile`/`~/.zprofile`/`~/.profile` 常不存在。只写"已存在"的会漏掉登录 shell；无条件创建会往 `$HOME` 丢新文件（NO UFO 原则边缘）。
  推荐：**已存在才写；`~/.profile` 例外——不存在则创建**（登录环境兜底，且是标准文件名）。
- fish 文件名：`conf.d/chsrc.fish`（单文件、内部按 dish 分 block）还是 `conf.d/chsrc-<tag>.fish`（每 dish 一文件）。
  推荐 **`chsrc.fish`**：block 机制已隔离各 dish，少造文件。

### D25 — fish 加载顺序陷阱
fish 先读 `conf.d/*.fish` 再读 `config.fish`。老版本 `rustup` 正是往 `config.fish` 写过 `set -x`（`rustup.c:103-104`），新实现写 `conf.d` → **老散行反而赢**。
- A. 仅按 D4 提示"检测到 config.fish 中有同名变量，请手工删除"
- B. 对迁移的 5 个 dish 做一次性精确清理（仅删同名且形态为 `set -x NAME <url>` 的行）
- 推荐 **B**，范围严格限于 5 个 dish 的已知变量名；A 会让提示永久驻留。

### D26 — v1 `ENDIN` 块是否完全不识别
D6 定为"忽略原有注释"。但 Homebrew 的 v1 块（`rawstr4c.h:9,11`）是**未注释的 `export` 真行**，老用户升级后它不会被清理，只是被后写的新块覆盖（shell 后赋值赢）。
- A. 完全不识别（残留，靠新块覆盖生效）
- B. 一次性识别并清理 `BEGIN`/`ENDIN` 旧块（约 15 行代码）
- 推荐 **B**。

### D27 — 模块如何拿到"当前 dish"，block 头参数从哪来
D7=A 要求模块内部取 dish 首别名，但 `waiter.c` 只调 `dish->setfn(option)`，框架**没有**"当前 dish"全局。
- A. 新增 `ProgStatus.CurrentDish`，由 `waiter_handle_Set_Source()`/`Reset` 在调用 `setfn` 前赋值
- B. 退回到 recipe 传参（违背 D7=A）
- 需要同时定：block 第二行的"源简写"取 `source.name` 还是 `source.provider`，`scope` 取解析后的实际 scope。
  推荐 **A**；参数写作可解析的 `# src=Tuna scope=user on=2026-10-28` 形式，供 D13 的 getsrc 反读。

### D28 — `ScopeCap_Able_But_Just_Docs` 的三项语义
新增枚举会触及 `struct.h:140-147`、`waiter.c:250-271`（`ls` 显示）、`source.c:812,824,836`（三处 `cap != ScopeCap_Able_And_Implemented` 判定）。必须定：
- 权限校验**放行还是拦截**？推荐**放行**（能力上可行，只是 chsrc 用文档指引代替实现）。
- `ls` 的标记与文案？推荐半角符号 `HalfYesMark` + 黄色 + "支持但仅提供文档指引"。
- 首批哪些 dish 用它（env 类 dish 的 ProjectScope 你已定为 `Unable`，那这个新枚举给谁用）？

### D29 — getsrc 侧 API 形状
`chsrc_view_env` 现在靠 `system("set X"/"echo X=$X")` 直接打印，不返回任何东西。按 D13 重做后：
- A. 保持 `void` 变参，内部自己读持久化值 + `getenv` 并打印（4 个调用点零改动）
- B. 拆成 `char *chsrc_get_env_persisted(name)` + 展示函数，让 recipe 能基于值判断（例如 `Env_Unchanged` 短路）
- 推荐 **A**（一期够用），但输出格式需要保留 `NAME=value` 一行一条，便于现有 CLI 测试断言。
- 附带：持久化值与临时值冲突时的中英提示文案。

### D30 — 引号与 fish 语法逐条确认
2.4 表的 4 条规则请逐条确认或推翻；并定 fish 用 `set -x`（仅配置文件语义）还是 `set -Ux`（universal，会另写 `~/.config/fish/fish_variables`，扩散面更大）。推荐 `set -x`。

### D31 — 你未回答的两项（原 D11、D21）
- 注册表值类型：`REG_SZ` 还是 `REG_EXPAND_SZ`。推荐 `REG_SZ`（URL 不含 `%`；`REG_EXPAND_SZ` 会让含 `%` 的值产生展开歧义）。
- 注册表 API 用 `A` 版还是 `W` 版。`W` 版需在 `env.c` 内新增 `static` UTF-8→UTF-16 转换（`MultiByteToWideChar`，xy.h 目前无任何宽字符 helper）；`A` 版与项目现有 `GetTempPathA`/`SHGetFolderPathA` 惯例一致，但非 ASCII 值按 ANSI 码页存储。推荐 **`W` 版 + static 转换**。

### D32 — `.reg` 备份细则
待定：确切目录（`GetTempPathA` 根 vs 其下 `chsrc-backup/` 子目录）、文件名模板（建议 `chsrc-env-backup-YYYYmmdd-HHMMSS.reg`）、`set(name, NULL)` 删除操作是否也备份（推荐**也备份**）、是否覆盖 HKLM、`.reg` 编码（UTF-16LE+BOM 是 `REGEDIT4`/`Windows Registry Editor Version 5.00` 的官方要求，推荐 UTF-16LE）。

### D33 — `OS.c:282` 的 `getenv ("$EUID")` bug
带 `$` 使该分支恒为 NULL，于是每次 `chsrc_ensure_root()` 都 fork 一次 `id -u`。
- A. 本分支单独一个 commit 修掉（顺手）
- B. 单独开 issue + PR，不混入模块化
- 推荐 **A**（改动 1 行，且新模块要用 `chsrc_is_root()`，两者必然同时出现）。

### D34 — 是否广播 `WM_SETTINGCHANGE`
广播只影响新起的 GUI/Explorer 子进程，不影响当前终端，与 D22"一律提示重启终端"不冲突。做了可省掉用户手动重启资源管理器；代价是多链 `-luser32` 并新增一次 `SendMessageTimeoutW(HWND_BROADCAST, ...)`（需短超时，失败静默）。推荐**做**。

### D35 — block 结束行是否带 tag
`# Ver2 CHSRC BLOCK ED` 不带标识时，若用户文件里存在游离的 ED 行，`OP of A` 会错配到第一个 ED。
- A. 结束行也带：`# Ver2 CHSRC BLOCK ED of XXX`（严格配对，起始/结束对称）
- B. 保持 `ED` 不带，扫描时遇到下一个任意 `OP` 即判定异常
- 推荐 **A**。

### D36 — 幂等短路与提示的关系
D5/D13 之后可以判定"新值 == 持久化值"。此时：
- 是否仍备份？推荐**不备份、不写盘**，返回 `Env_Unchanged`。
- 是否仍提示重启终端（D22）？推荐 **`Env_Unchanged` 不提示**（无变化却要求重启很怪），只在 `Env_OK` 时提示。

---

## 4. 实施阶段

### P1 内核层（`src/framework/block.c`，新文件）
1. Ver2 block 解析/切块/upsert/remove、多块合并、`chsrc_block_has_var`。
2. `chsrc-main.c` include 顺序：`framework/OS.c`(38) → 新增 `framework/block.c` → 新增 `framework/env.c` → 早于 `recipe/menu.c`(105)。
- **验收**：`just bd` 通过；纯字符串级单测（`test/fw.c` 里构造内存串，不碰磁盘）。

### P2 POSIX 后端（`src/framework/env.c`，新文件）
3. `chsrc_get_env`、name/value 校验、按 2.2/2.4 生成正文并 upsert、写入集/检查集分派、冲突提醒、备份、dry-run、`Env_Unchanged` 短路。
4. System scope：`chsrc_is_root()`（D18）+ 权限自检 → 不足即按 D8 打印指引并中止；`/etc/environment` 纯 `KEY=VALUE`。
5. `chsrc_view_env` 迁入并按 D13 重做（保留 `OS.c:641` 的变参形态）。
- **验收**：`HOME` 指向临时目录沙箱内，连续两次 `chsrc set rustup` → block 数 == 1、`.bak` 数 == 1；换 mirror 一次 → 仍 == 1；`get rustup` 显示持久化值。

### P3 Windows 后端
6. 注册表读/写/删（D31 定的 A/W 与值类型）、`.reg` 导出与回滚（D17/D32）、可选广播（D34）。
7. 显式链接清单落地（2.5），**先写一个 10 行探针确认 MinGW-w64 是否真的需要显式 `-ladvapi32`**，再统一改。
- **验收**：原生 Windows 连续两次 set 后 `reg query HKCU\Environment` 仅一条；`set(name,NULL)` 后值消失；`just test` 在 MSYS2 下可编译可跑。

### P4 recipe 迁移（5 个，各 1 commit）
8. `rustup.c`：删 66-106 全部分支 →
   ```c
   chsrc_set_env_block ("RUSTUP_DIST_SERVER", source.url,
                        "RUSTUP_UPDATE_ROOT", xy_2strcat (source.url, "/rustup"),
                        NULL);
   ```
   删 110 的手写重启提示（D22）。
9. `Homebrew.c`：删 60-79，`rawstr4c` 只留 block 正文模板；删 28-29 的"手动清理"note。
10. `Pub.c` / `Flutter.c` / `nvm.c`：同形收敛；nvm 顺带获得原本缺失的备份与 fish 支持。
11. 5 个 dish 的 `chef_set_scope_cap` 按 D9/D10 重标（Project → `Unable`，User → `Able_And_Implemented`，System 视 D8 实现情况）。
- **验收**：`git grep -E 'setx|^.*export |set -x' src/recipe/` 命中 0（`rawstr4c.md` 文档镜像除外）。

### P5 测试、文档
12. `test/fw.c`：include `block.c`/`env.c`（该文件目前只 include `base.c`+`OS.c`，见 `test/fw.c:15-16`），加 block 单测与 `HOME` 沙箱集成测试。
13. `test/cli.pl`：幂等回归用例（连跑两次 set，断言 block 计数）。
14. 文档：`doc/10-如何编写recipe.md`（96 行函数清单处补 env 说明 + block 格式规范）、`.github/copilot-instructions.md` 架构速览加 `framework/block.c`/`framework/env.c`、`README.md:290` 补 Windows 显式链接编译示例、`doc/chsrc.1` 与 `doc/chsrc.texi` 若含 scope 描述则同步。
15. 全部 locale 一致性：本模块的用户文案走 `CHINESE ? ... : ...` 现有惯例，无额外 locale 文件。

### commit 切分（D20，单 PR）
`P1 block 内核` → `P2 POSIX 后端` → `P3 Windows 后端 + 显式链接` → `P4 迁移×5` → `P5 测试 + 文档` →（若 D33=A）`chsrc_ensure_root 的 $EUID 修复` 放最前。

---

## 5. 风险与对策

1. **fish 覆盖顺序**（D25）：不处理会让迁移后的 rustup 在 fish 用户上静默失效。
2. **v1 旧块残留**（D26）：不影响生效，但 Homebrew 用户文件里会留一份过期 URL。
3. **combo dish 备份次数**：`ProgStatus.ComboBackedUpPaths`（`OS.c:892-904`）已去重，多 block 写同文件时须复核次数仍为 1。
4. **`/etc/environment` 语法**：写成 `export` 或带引号会破坏 pam_env 解析，影响面比 rc 文件大得多；按 2.4 严格纯 `KEY=VALUE`。
5. **`ScopeCap` 新枚举**：`source.c` 三处判定 + `waiter.c` switch 必须同步，漏一处就是 `xy_unreached()` 崩溃（`waiter.c:270`）。
6. **注册表广播**：`HWND_BROADCAST` 超时需短（≤3000ms），失败静默，不能因广播阻塞而报错。
7. **升级面**：本次不动 `resetsrc` 语义（D19），已有 recipe 的 reset 行为保持；但 `chsrc_view_env` 的输出格式变化可能影响 `test/cli.pl` 断言，P5 需一并更新。

---

## 6. 基线事实（已核实，供复核）

- 真正持久化环境变量的 recipe 只有 5 个：`rustup.c`、`Dart/Pub.c`、`Dart/Flutter.c`、`JavaScript/nvm.c`、`ware/Homebrew/Homebrew.c`；`chsrc_view_env` 使用点 5 处（含上述 4 个 getsrc）。
- `xy.h:1358-1360` 只有 `~/.zshrc`/`~/.bashrc`/`~/.config/fish/config.fish` 三个常量，**没有** `~/.profile`、conf.d、fish_variables 相关定义 → P2 需在 framework 内自备路径。
- `xy.h` 内**无**任何宽字符转换 helper、**无** `setenv`/`_putenv` 封装。
- `xy_str_gsub`（`xy.h:279`）是子串替换，`abc` 会命中 `abcd` → 本模块禁止用它做值覆写。
- 逐行内存编辑的先例：`src/recipe/lang/Python/uv/toml.c:24` 的 `mktoml_next_line` 路线。
