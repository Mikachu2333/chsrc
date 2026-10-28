/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * -------------------------------------------------------------
 * File Name     : env.c
 * File Authors  : @Mikachu2333
 * Contributors  : @ccmywish
 *               |
 * Created On    : <2026-10-28>
 * Last Modified : <2026-10-28>
 *
 * 跨平台环境变量持久化管理模块
 * ------------------------------------------------------------*/

typedef enum EnvOp_t
{
  Env_OK,                /* 实际发生写入或删除 */
  Env_Unchanged,         /* 所有目标持久化值已与期望一致，未备份、未写盘 */
  Env_NoPermission,      /* SystemScope 权限不足，未做任何修改 */
  Env_TargetUnwritable,  /* 目标文件/目录/注册表键不可写或备份创建失败 */
  Env_BadName,           /* 变量名不匹配 ^[A-Za-z_][A-Za-z0-9_]*$ */
  Env_BadValue,          /* 值含目标方言危险字符（引号/$/`/换行等） */
  Env_AmbiguousTarget,   /* 未知 shell、多文件值冲突或现有行含复杂动态语法 */
  Env_ScopeUnsupported   /* ProjectScope 等不支持的作用域（防御性） */
}
EnvOp_t;

typedef struct EnvKV_t
{
  const char *name;
  const char *value;     /* NULL 表示删除该变量；"" 表示设为空串 */
}
EnvKV_t;

typedef enum PosixShell_t
{
  PosixShell_Unknown = 0,
  PosixShell_Bash    = 1,
  PosixShell_Zsh     = 2,
  PosixShell_Fish    = 3
}
PosixShell_t;

typedef enum EnvDialect_t
{
  EnvDialect_PosixShell,
  EnvDialect_Fish,
  EnvDialect_EtcEnv,
  EnvDialect_WinRegistry
}
EnvDialect_t;

typedef enum EnvLineMatch_t
{
  EnvLine_NoMatch,
  EnvLine_SafeMatch,
  EnvLine_ComplexMatch
}
EnvLineMatch_t;

/* 仅供 test/fw.c 单元测试注入使用，生产环境保持默认值 */
static int         Env_Test_Force_Shell  = -1;
static const char *Env_Test_Etc_Env_Path = NULL;


/* ============================================================
 * 动态字节缓冲与字符串切片辅助
 * ============================================================ */

static char *
env_strndup (const char *s, size_t n)
{
  char *out = xy_malloc0 (n + 1);
  if (n > 0)
    memcpy (out, s, n);
  out[n] = '\0';
  return out;
}

typedef struct EnvBuf_t
{
  char   *data;
  size_t  len;
  size_t  cap;
}
EnvBuf_t;

static void
env_buf_init (EnvBuf_t *b)
{
  b->cap  = 256;
  b->len  = 0;
  b->data = xy_malloc0 (b->cap);
}

static void
env_buf_reserve (EnvBuf_t *b, size_t extra)
{
  size_t needed = b->len + extra + 1;
  if (needed <= b->cap)
    return;

  size_t new_cap = b->cap;
  while (new_cap < needed)
    {
      new_cap *= 2;
    }

  char *new_data = realloc (b->data, new_cap);
  if (!new_data)
    {
      xy_throw ("Memory allocation failed in env_buf_reserve");
    }
  b->data = new_data;
  b->cap  = new_cap;
}

static void
env_buf_append_mem (EnvBuf_t *b, const char *s, size_t n)
{
  if (0 == n)
    return;
  env_buf_reserve (b, n);
  memcpy (b->data + b->len, s, n);
  b->len += n;
  b->data[b->len] = '\0';
}

static void
env_buf_append_str (EnvBuf_t *b, const char *s)
{
  if (!s)
    return;
  env_buf_append_mem (b, s, strlen (s));
}

static void
env_buf_append_char (EnvBuf_t *b, char c)
{
  env_buf_reserve (b, 1);
  b->data[b->len++] = c;
  b->data[b->len]   = '\0';
}

static char *
env_buf_take (EnvBuf_t *b)
{
  char *ret = b->data;
  b->data   = NULL;
  b->len    = 0;
  b->cap    = 0;
  return ret;
}

static void
env_buf_free (EnvBuf_t *b)
{
  if (b->data)
    {
      free (b->data);
      b->data = NULL;
    }
  b->len = 0;
  b->cap = 0;
}


/* ============================================================
 * 变量名与变量值合法性校验（防注入）
 * ============================================================ */

static bool
env_is_ident_start (char c)
{
  return (   ('A' <= c && c <= 'Z')
          || ('a' <= c && c <= 'z')
          || ('_' == c));
}

static bool
env_is_ident_char (char c)
{
  return (   env_is_ident_start (c)
          || ('0' <= c && c <= '9'));
}

static bool
env_validate_name (const char *name)
{
  if (!name || '\0' == name[0])
    return false;

  if (!env_is_ident_start (name[0]))
    return false;

  for (size_t i = 1; name[i] != '\0'; i++)
    {
      if (!env_is_ident_char (name[i]))
        return false;
    }
  return true;
}

static bool
env_validate_value (const char *value, EnvDialect_t dialect, char *out_bad_ch)
{
  if (NULL == value)
    return true; /* NULL 表示删除变量 */

  for (size_t i = 0; value[i] != '\0'; i++)
    {
      unsigned char uc = (unsigned char) value[i];
      char c = value[i];

      /* 所有平台均禁止换行与回车及控制字符（除制表符视方言而定） */
      if ('\r' == c || '\n' == c || (uc < 0x20 && '\t' != c) || 0x7F == uc)
        {
          if (out_bad_ch) *out_bad_ch = c;
          return false;
        }

      if (EnvDialect_WinRegistry == dialect)
        {
          continue;
        }

      /* POSIX Shell / fish / /etc/environment 共同危险字符 */
      if ('$' == c || '`' == c || '"' == c || '\\' == c)
        {
          if (out_bad_ch) *out_bad_ch = c;
          return false;
        }

      /* /etc/environment 使用无引号 NAME=value，额外禁止空白符、# 与单引号 */
      if (EnvDialect_EtcEnv == dialect)
        {
          if (' ' == c || '\t' == c || '#' == c || '\'' == c)
            {
              if (out_bad_ch) *out_bad_ch = c;
              return false;
            }
        }
    }

  return true;
}


/* ============================================================
 * POSIX 行级语法精确识别与改写引擎
 * ============================================================ */

static const char *
env_skip_ws (const char *p)
{
  while (' ' == *p || '\t' == *p)
    {
      p++;
    }
  return p;
}

/**
 * 检查值结尾后是否仅剩空白或合法行尾注释
 * @param require_ws_before_hash  POSIX shell 中引号/裸值后必须有空白才能将 # 视为注释
 */
static bool
env_valid_line_trailer (const char *p, bool require_ws_before_hash)
{
  if ('\0' == *p)
    return true;

  if (require_ws_before_hash && (' ' != *p && '\t' != *p))
    return false;

  p = env_skip_ws (p);
  return ('\0' == *p || '#' == *p);
}

static bool
env_is_unquoted_bad_shell_char (char c)
{
  return (   '$' == c || '`' == c || '"' == c || '\'' == c || '\\' == c
          || ';' == c || '&' == c || '|' == c || '<'  == c || '>'  == c
          || '(' == c || ')' == c || '*' == c || '?'  == c || '['  == c
          || ']' == c || '~' == c);
}

/**
 * 解析 NAME= 右侧字面值（供 PosixShell 与 EtcEnv 共用）
 */
static EnvLineMatch_t
env_parse_posix_rhs (const char *rhs, EnvDialect_t dialect, char **out_val)
{
  if ('\0' == *rhs)
    {
      if (out_val) *out_val = xy_strdup ("");
      return EnvLine_SafeMatch;
    }

  /* NAME= 后直接跟空白：仅当后面为空或 # 注释时才表示空串，否则如 `NAME= cmd` 属于命令前缀 */
  if (' ' == *rhs || '\t' == *rhs)
    {
      const char *after_ws = env_skip_ws (rhs);
      if ('\0' == *after_ws || '#' == *after_ws)
        {
          if (out_val) *out_val = xy_strdup ("");
          return EnvLine_SafeMatch;
        }
      return EnvLine_ComplexMatch;
    }

  if ('#' == *rhs)
    {
      /* `NAME=#foo` 在 shell 中并非注释而是字面 `#foo`，但对 /etc/environment 存在歧义 */
      if (EnvDialect_EtcEnv == dialect)
        return EnvLine_ComplexMatch;
    }

  if ('"' == *rhs)
    {
      const char *start = rhs + 1;
      const char *p     = start;
      while ('\0' != *p && '"' != *p)
        {
          if ('$' == *p || '`' == *p || '\\' == *p)
            return EnvLine_ComplexMatch;
          p++;
        }
      if ('"' != *p)
        return EnvLine_ComplexMatch; /* 未闭合双引号 */

      if (!env_valid_line_trailer (p + 1, true))
        return EnvLine_ComplexMatch;

      if (out_val) *out_val = env_strndup (start, (size_t) (p - start));
      return EnvLine_SafeMatch;
    }

  if ('\'' == *rhs)
    {
      const char *start = rhs + 1;
      const char *p     = start;
      while ('\0' != *p && '\'' != *p)
        {
          /* 若单引号内含有双引号写出时不安全的字符，拒绝自动改写以防语义变化 */
          if ('$' == *p || '`' == *p || '"' == *p || '\\' == *p)
            return EnvLine_ComplexMatch;
          p++;
        }
      if ('\'' != *p)
        return EnvLine_ComplexMatch; /* 未闭合单引号 */

      if (!env_valid_line_trailer (p + 1, true))
        return EnvLine_ComplexMatch;

      if (out_val) *out_val = env_strndup (start, (size_t) (p - start));
      return EnvLine_SafeMatch;
    }

  /* 裸值形式（如 export NVM_NODEJS_ORG_MIRROR=https://... 或 /etc/environment 的 K=V） */
  const char *start = rhs;
  const char *p     = start;
  while ('\0' != *p && ' ' != *p && '\t' != *p)
    {
      if (env_is_unquoted_bad_shell_char (*p))
        return EnvLine_ComplexMatch;
      if (EnvDialect_EtcEnv == dialect && '#' == *p)
        return EnvLine_ComplexMatch;
      p++;
    }

  if (!env_valid_line_trailer (p, true))
    return EnvLine_ComplexMatch;

  if (out_val) *out_val = env_strndup (start, (size_t) (p - start));
  return EnvLine_SafeMatch;
}

/**
 * 检查一行中在 export 之后是否以任意形式出现了目标变量名（用于捕获多变量同行定义，如 `export A=1 TARGET=2`）
 */
static bool
env_export_line_mentions_name (const char *p, const char *name, size_t name_len)
{
  bool in_squote = false;
  bool in_dquote = false;

  while ('\0' != *p)
    {
      if (!in_squote && !in_dquote && '#' == *p)
        break;

      if ('\'' == *p && !in_dquote)
        {
          in_squote = !in_squote;
          p++;
          continue;
        }
      if ('"' == *p && !in_squote)
        {
          in_dquote = !in_dquote;
          p++;
          continue;
        }
      if ('\\' == *p && !in_squote && '\0' != p[1])
        {
          p += 2;
          continue;
        }

      if (!in_squote && !in_dquote && env_is_ident_start (*p))
        {
          const char *id_start = p;
          while (env_is_ident_char (*p))
            p++;
          size_t id_len = (size_t) (p - id_start);
          if (   id_len == name_len
              && 0 == memcmp (id_start, name, name_len)
              && ('=' == *p || ' ' == *p || '\t' == *p || '\0' == *p || ';' == *p))
            {
              return true;
            }
          continue;
        }

      p++;
    }
  return false;
}

static EnvLineMatch_t
env_classify_posix_or_etc_line (const char *line, size_t line_len,
                                const char *name, EnvDialect_t dialect,
                                char **out_val)
{
  /* 行内含有嵌入 NUL 或孤立 CR 视为异常，仅在疑似提及时拒绝，否则忽略 */
  if (memchr (line, '\0', line_len) || memchr (line, '\r', line_len))
    {
      char *copy = xy_malloc0 (line_len + 1);
      for (size_t i = 0; i < line_len; i++)
        copy[i] = ('\0' == line[i]) ? ' ' : line[i];
      bool mentions = (NULL != strstr (copy, name));
      free (copy);
      return mentions ? EnvLine_ComplexMatch : EnvLine_NoMatch;
    }

  char *buf = env_strndup (line, line_len);
  const char *p = env_skip_ws (buf);
  size_t name_len = strlen (name);

  if ('\0' == *p || '#' == *p)
    {
      free (buf);
      return EnvLine_NoMatch;
    }

  bool had_export = false;
  if (0 == strncmp (p, "export", 6) && (' ' == p[6] || '\t' == p[6]))
    {
      had_export = true;
      p = env_skip_ws (p + 6);
    }

  /* 直接以 NAME= 开头 */
  if (0 == strncmp (p, name, name_len) && '=' == p[name_len])
    {
      EnvLineMatch_t res = env_parse_posix_rhs (p + name_len + 1, dialect, out_val);
      free (buf);
      return res;
    }

  /* `export NAME`（无 `=`）或 `export OTHER=1 NAME=2` 等复杂形式 */
  if (had_export && env_export_line_mentions_name (p, name, name_len))
    {
      free (buf);
      return EnvLine_ComplexMatch;
    }

  free (buf);
  return EnvLine_NoMatch;
}

static EnvLineMatch_t
env_classify_fish_line (const char *line, size_t line_len,
                        const char *name, char **out_val)
{
  if (memchr (line, '\0', line_len) || memchr (line, '\r', line_len))
    {
      char *copy = xy_malloc0 (line_len + 1);
      for (size_t i = 0; i < line_len; i++)
        copy[i] = ('\0' == line[i]) ? ' ' : line[i];
      bool mentions = (NULL != strstr (copy, name));
      free (copy);
      return mentions ? EnvLine_ComplexMatch : EnvLine_NoMatch;
    }

  char *buf = env_strndup (line, line_len);
  const char *p = env_skip_ws (buf);
  size_t name_len = strlen (name);

  if ('\0' == *p || '#' == *p)
    {
      free (buf);
      return EnvLine_NoMatch;
    }

  /* 必须是独立的 `set` 命令 */
  if (0 != strncmp (p, "set", 3) || (' ' != p[3] && '\t' != p[3]))
    {
      free (buf);
      return EnvLine_NoMatch;
    }

  p = env_skip_ws (p + 3);

  bool has_export   = false;
  bool has_bad_flag = false;

  while ('-' == *p)
    {
      if ('-' == p[1] && (' ' == p[2] || '\t' == p[2] || '\0' == p[2]))
        {
          p = env_skip_ws (p + 2);
          break;
        }
      else if ('-' == p[1])
        {
          /* 长选项 */
          const char *opt_start = p;
          while ('\0' != *p && ' ' != *p && '\t' != *p)
            p++;
          size_t opt_len = (size_t) (p - opt_start);
          if (8 == opt_len && 0 == memcmp (opt_start, "--export", 8))
            has_export = true;
          else if (8 == opt_len && 0 == memcmp (opt_start, "--global", 8))
            xy_noop ();
          else
            has_bad_flag = true;
        }
      else
        {
          /* 短选项组合，如 -x, -gx, -xg, -Ux, -e */
          p++;
          if ('\0' == *p || ' ' == *p || '\t' == *p)
            {
              has_bad_flag = true;
            }
          while ('\0' != *p && ' ' != *p && '\t' != *p)
            {
              if ('x' == *p)
                has_export = true;
              else if ('g' == *p)
                xy_noop ();
              else
                has_bad_flag = true;
              p++;
            }
        }
      p = env_skip_ws (p);
    }

  /* 读取变量名 token */
  const char *var_start = p;
  while ('\0' != *p && ' ' != *p && '\t' != *p && ';' != *p && '#' != *p)
    p++;
  size_t var_len = (size_t) (p - var_start);

  if (0 == var_len)
    {
      free (buf);
      return EnvLine_NoMatch;
    }

  bool matched_var = (var_len == name_len && 0 == memcmp (var_start, name, name_len));
  if (!matched_var)
    {
      /* 如 `set -e OTHER TARGET` 一类多变量操作 */
      if (has_bad_flag && env_export_line_mentions_name (p, name, name_len))
        {
          free (buf);
          return EnvLine_ComplexMatch;
        }
      free (buf);
      return EnvLine_NoMatch;
    }

  /* 命中了变量名，但选项不是纯导出（如 -Ux universal、-e erase、无 -x 等），拒绝自动覆写 */
  if (!has_export || has_bad_flag)
    {
      free (buf);
      return EnvLine_ComplexMatch;
    }

  if (';' == *p)
    {
      free (buf);
      return EnvLine_ComplexMatch;
    }

  p = env_skip_ws (p);
  if ('\0' == *p || '#' == *p)
    {
      /* `set -x NAME` 无参数在 fish 中为空列表而非空串，视为复杂语法保护 */
      free (buf);
      return EnvLine_ComplexMatch;
    }

  /* 解析单个值参数 */
  if ('"' == *p)
    {
      const char *vstart = p + 1;
      const char *vp     = vstart;
      while ('\0' != *vp && '"' != *vp)
        {
          if ('$' == *vp || '`' == *vp || '\\' == *vp)
            {
              free (buf);
              return EnvLine_ComplexMatch;
            }
          vp++;
        }
      if ('"' != *vp || !env_valid_line_trailer (vp + 1, true))
        {
          free (buf);
          return EnvLine_ComplexMatch;
        }
      if (out_val) *out_val = env_strndup (vstart, (size_t) (vp - vstart));
      free (buf);
      return EnvLine_SafeMatch;
    }

  if ('\'' == *p)
    {
      const char *vstart = p + 1;
      const char *vp     = vstart;
      while ('\0' != *vp && '\'' != *vp)
        {
          if ('$' == *vp || '`' == *vp || '"' == *vp || '\\' == *vp)
            {
              free (buf);
              return EnvLine_ComplexMatch;
            }
          vp++;
        }
      if ('\'' != *vp || !env_valid_line_trailer (vp + 1, true))
        {
          free (buf);
          return EnvLine_ComplexMatch;
        }
      if (out_val) *out_val = env_strndup (vstart, (size_t) (vp - vstart));
      free (buf);
      return EnvLine_SafeMatch;
    }

  const char *vstart = p;
  const char *vp     = vstart;
  while ('\0' != *vp && ' ' != *vp && '\t' != *vp)
    {
      if (env_is_unquoted_bad_shell_char (*vp) || '#' == *vp)
        {
          free (buf);
          return EnvLine_ComplexMatch;
        }
      vp++;
    }
  if (!env_valid_line_trailer (vp, true))
    {
      free (buf);
      return EnvLine_ComplexMatch;
    }

  if (out_val) *out_val = env_strndup (vstart, (size_t) (vp - vstart));
  free (buf);
  return EnvLine_SafeMatch;
}

static EnvLineMatch_t
env_classify_line (const char *line, size_t line_len,
                   const char *name, EnvDialect_t dialect,
                   char **out_val)
{
  if (out_val) *out_val = NULL;

  switch (dialect)
    {
    case EnvDialect_PosixShell:
    case EnvDialect_EtcEnv:
      return env_classify_posix_or_etc_line (line, line_len, name, dialect, out_val);
    case EnvDialect_Fish:
      return env_classify_fish_line (line, line_len, name, out_val);
    default:
      xy_unreached ();
      return EnvLine_NoMatch;
    }
}

static char *
env_format_canonical_line (const char *name, const char *value, EnvDialect_t dialect)
{
  switch (dialect)
    {
    case EnvDialect_PosixShell:
      return xy_strcat (5, "export ", name, "=\"", value, "\"");
    case EnvDialect_Fish:
      return xy_strcat (5, "set -x ", name, " \"", value, "\"");
    case EnvDialect_EtcEnv:
      return xy_strcat (3, name, "=", value);
    default:
      xy_unreached ();
      return NULL;
    }
}


/* ============================================================
 * 文件读写与内容转换
 * ============================================================ */

static bool
env_read_file_bytes (const char *norm_path, char **out_buf, size_t *out_len)
{
  *out_buf = NULL;
  *out_len = 0;

  FILE *fp = fopen (norm_path, "rb");
  if (!fp)
    return false;

  EnvBuf_t b;
  env_buf_init (&b);

  char chunk[4096];
  size_t n = 0;
  while ((n = fread (chunk, 1, sizeof (chunk), fp)) > 0)
    {
      env_buf_append_mem (&b, chunk, n);
    }

  if (ferror (fp))
    {
      fclose (fp);
      env_buf_free (&b);
      return false;
    }

  fclose (fp);
  *out_len = b.len;
  *out_buf = env_buf_take (&b);
  return true;
}

/**
 * 在内存中对单个配置文件内容应用 kvs 修改：
 * - 第 1 处安全匹配原位替换为标准格式；
 * - 同文件后续重复安全匹配行剔除；
 * - value == NULL 删除所有安全匹配行；
 * - 未出现且 value != NULL 的变量追加在文件末尾（确保前导换行）；
 * - 遇到复杂动态行立即返回 Env_AmbiguousTarget 并携带精确文件路径与行号。
 */
static EnvOp_t
env_transform_content (const char   *old_content,
                       size_t        old_len,
                       EnvDialect_t  dialect,
                       const EnvKV_t *kvs,
                       size_t        kv_count,
                       const char   *display_path,
                       char        **out_new_content,
                       size_t       *out_new_len,
                       bool         *out_changed,
                       char        **out_err)
{
  *out_new_content = NULL;
  *out_new_len     = 0;
  *out_changed     = false;

  bool *seen = xy_malloc0 (kv_count > 0 ? kv_count * sizeof (bool) : 1);
  EnvBuf_t out;
  env_buf_init (&out);

  size_t pos    = 0;
  size_t lineno = 0;

  while (pos < old_len)
    {
      lineno++;
      const char *nl = memchr (old_content + pos, '\n', old_len - pos);
      size_t raw_len = nl ? (size_t) (nl - (old_content + pos) + 1) : (old_len - pos);
      const char *raw_line = old_content + pos;

      size_t body_len = raw_len;
      const char *eol = "\n";
      size_t eol_len  = 1;

      if (body_len > 0 && '\n' == raw_line[body_len - 1])
        {
          body_len--;
          if (body_len > 0 && '\r' == raw_line[body_len - 1])
            {
              body_len--;
              eol     = "\r\n";
              eol_len = 2;
            }
        }

      int matched_idx = -1;
      for (size_t k = 0; k < kv_count; k++)
        {
          EnvLineMatch_t m = env_classify_line (raw_line, body_len, kvs[k].name, dialect, NULL);
          if (EnvLine_ComplexMatch == m)
            {
              char *line_str = xy_int2str ((int) lineno);
              if (out_err)
                {
                  *out_err = ENGLISH
                    ? xy_strcat (7, "Complex or dynamic syntax for `", kvs[k].name,
                                 "` in ", display_path, ":", line_str,
                                 "; refusing automatic modification to protect your configuration. Please edit it manually.")
                    : xy_strcat (7, "配置文件 ", display_path, ":", line_str,
                                 " 处的变量 `", kvs[k].name,
                                 "` 含有动态展开或复杂语法，为保护您的配置已拒绝自动修改，请手工编辑");
                }
              free (line_str);
              free (seen);
              env_buf_free (&out);
              return Env_AmbiguousTarget;
            }
          if (EnvLine_SafeMatch == m && matched_idx < 0)
            {
              matched_idx = (int) k;
            }
        }

      if (matched_idx < 0)
        {
          /* 无关行（含注释、历史 # ------ chsrc BLOCK 注释等）原样保留 */
          env_buf_append_mem (&out, raw_line, raw_len);
        }
      else
        {
          size_t k = (size_t) matched_idx;
          if (NULL == kvs[k].value)
            {
              /* 删除该行 */
              xy_noop ();
            }
          else if (!seen[k])
            {
              seen[k] = true;
              char *canon = env_format_canonical_line (kvs[k].name, kvs[k].value, dialect);
              env_buf_append_str (&out, canon);
              env_buf_append_mem (&out, eol, eol_len);
              free (canon);
            }
          else
            {
              /* 同文件后续重复定义直接折叠剔除 */
              xy_noop ();
            }
        }

      pos += raw_len;
    }

  /* 追加文件中尚未出现且非删除的变量 */
  for (size_t k = 0; k < kv_count; k++)
    {
      if (NULL != kvs[k].value && !seen[k])
        {
          if (out.len > 0 && '\n' != out.data[out.len - 1])
            {
              env_buf_append_char (&out, '\n');
            }
          char *canon = env_format_canonical_line (kvs[k].name, kvs[k].value, dialect);
          env_buf_append_str (&out, canon);
          env_buf_append_char (&out, '\n');
          free (canon);
        }
    }

  free (seen);

  bool changed = (out.len != old_len) || (old_len > 0 && 0 != memcmp (old_content, out.data, old_len));
  *out_changed     = changed;
  *out_new_len     = out.len;
  *out_new_content = env_buf_take (&out);
  return Env_OK;
}


/* ============================================================
 * POSIX Shell 检测与配置文件集合
 * ============================================================ */

#ifndef XY_Build_On_Windows
#include <sys/types.h>

static PosixShell_t
env_shell_from_name (const char *raw_name, bool *out_known_unsupported)
{
  if (out_known_unsupported) *out_known_unsupported = false;
  if (!raw_name || '\0' == raw_name[0])
    return PosixShell_Unknown;

  /* 取路径 basename */
  const char *base = strrchr (raw_name, '/');
  base = base ? (base + 1) : raw_name;

  /* 登录 shell 可能带有前导 '-'，如 -zsh、-bash */
  while ('-' == *base)
    base++;

  if (xy_streql (base, "bash"))
    return PosixShell_Bash;
  if (xy_streql (base, "zsh"))
    return PosixShell_Zsh;
  if (xy_streql (base, "fish"))
    return PosixShell_Fish;

  if (   xy_streql (base, "csh")      || xy_streql (base, "tcsh")
      || xy_streql (base, "nu")       || xy_streql (base, "nushell")
      || xy_streql (base, "pwsh")     || xy_streql (base, "powershell")
      || xy_streql (base, "elvish")   || xy_streql (base, "xonsh")
      || xy_streql (base, "ksh")      || xy_streql (base, "mksh"))
    {
      if (out_known_unsupported) *out_known_unsupported = true;
    }

  return PosixShell_Unknown;
}

static char *
env_get_parent_comm (void)
{
  pid_t ppid = getppid ();
  if (ppid <= 1)
    return NULL;

  /* Linux 优先直读 /proc/<ppid>/comm，零子进程开销且安静 */
  char proc_path[64];
  snprintf (proc_path, sizeof (proc_path), "/proc/%d/comm", (int) ppid);
  FILE *fp = fopen (proc_path, "r");
  if (fp)
    {
      char line[128];
      if (fgets (line, sizeof (line), fp))
        {
          fclose (fp);
          return xy_str_strip (line);
        }
      fclose (fp);
    }

  /* macOS / BSD 回退至 ps */
  char ps_cmd[96];
  snprintf (ps_cmd, sizeof (ps_cmd), "ps -p %d -o comm= 2>/dev/null", (int) ppid);
  char *out = xy_run (ps_cmd, 1);
  if (out)
    {
      char *stripped = xy_str_strip (out);
      free (out);
      if ('\0' == stripped[0])
        {
          free (stripped);
          return NULL;
        }
      return stripped;
    }
  return NULL;
}

static PosixShell_t
env_detect_posix_shell (void)
{
  if (Env_Test_Force_Shell >= 0)
    return (PosixShell_t) Env_Test_Force_Shell;

  /* 1. 优先检查直接父进程名（支持用户在 bash 登录下临时进入交互式 zsh/fish/nu） */
  char *ppid_comm = env_get_parent_comm ();
  if (ppid_comm)
    {
      bool unsupported_interactive = false;
      PosixShell_t s = env_shell_from_name (ppid_comm, &unsupported_interactive);
      free (ppid_comm);
      if (PosixShell_Unknown != s)
        return s;
      if (unsupported_interactive)
        return PosixShell_Unknown;
    }

  /* 2. 父进程为 sh/dash 包装器或脚本解释器时，回退精确匹配 $SHELL */
  const char *shell_env = getenv ("SHELL");
  if (shell_env && '\0' != shell_env[0])
    {
      return env_shell_from_name (shell_env, NULL);
    }

  return PosixShell_Unknown;
}

static bool
env_is_migrated_legacy_fish_var (const char *name)
{
  static const char *const legacy_vars[] = {
    "RUSTUP_DIST_SERVER",
    "RUSTUP_UPDATE_ROOT",
    "PUB_HOSTED_URL",
    "FLUTTER_STORAGE_BASE_URL",
    "NVM_NODEJS_ORG_MIRROR",
    "HOMEBREW_API_DOMAIN",
    "HOMEBREW_BOTTLE_DOMAIN",
    "HOMEBREW_BREW_GIT_REMOTE",
    "HOMEBREW_CORE_GIT_REMOTE"
  };

  for (size_t i = 0; i < sizeof (legacy_vars) / sizeof (legacy_vars[0]); i++)
    {
      if (xy_streql (name, legacy_vars[i]))
        return true;
    }
  return false;
}

static char *
env_build_unknown_shell_error (const EnvKV_t *kvs, size_t kv_count)
{
  const char *shell_env = getenv ("SHELL");
  const char *shown     = (shell_env && '\0' != shell_env[0]) ? shell_env : "(unset)";

  EnvBuf_t hint;
  env_buf_init (&hint);

  if (ENGLISH)
    {
      env_buf_append_str (&hint, "Unable to confirm current shell (SHELL=");
      env_buf_append_str (&hint, shown);
      env_buf_append_str (&hint, "; supported shells: bash, zsh, fish). Refusing to guess target rc file.");
      if (kvs && kv_count > 0)
        {
          env_buf_append_str (&hint, " Please configure the following variable(s) manually in your shell profile:");
          for (size_t i = 0; i < kv_count; i++)
            {
              env_buf_append_str (&hint, "\n  ");
              if (kvs[i].value)
                {
                  env_buf_append_str (&hint, kvs[i].name);
                  env_buf_append_str (&hint, "=\"");
                  env_buf_append_str (&hint, kvs[i].value);
                  env_buf_append_str (&hint, "\"");
                }
              else
                {
                  env_buf_append_str (&hint, "unset ");
                  env_buf_append_str (&hint, kvs[i].name);
                }
            }
        }
    }
  else
    {
      env_buf_append_str (&hint, "无法精确确认当前 Shell 类型 (SHELL=");
      env_buf_append_str (&hint, shown);
      env_buf_append_str (&hint, "，当前仅自动支持 bash、zsh、fish)，已拒绝猜测写入配置文件。");
      if (kvs && kv_count > 0)
        {
          env_buf_append_str (&hint, "请手工在您的 Shell 配置文件中设置以下环境变量:");
          for (size_t i = 0; i < kv_count; i++)
            {
              env_buf_append_str (&hint, "\n  ");
              if (kvs[i].value)
                {
                  env_buf_append_str (&hint, kvs[i].name);
                  env_buf_append_str (&hint, "=\"");
                  env_buf_append_str (&hint, kvs[i].value);
                  env_buf_append_str (&hint, "\"");
                }
              else
                {
                  env_buf_append_str (&hint, "unset ");
                  env_buf_append_str (&hint, kvs[i].name);
                }
            }
        }
    }

  return env_buf_take (&hint);
}

typedef struct PosixFileSpec_t
{
  const char  *display_path;
  EnvDialect_t dialect;
  bool         create_if_missing;
}
PosixFileSpec_t;

typedef struct PreparedFileOp_t
{
  const char *display_path;
  char       *norm_path;
  bool        existed;
  bool        changed;
  char       *new_content;
}
PreparedFileOp_t;

static void
env_free_prepared_ops (PreparedFileOp_t *ops, size_t count)
{
  if (!ops)
    return;
  for (size_t i = 0; i < count; i++)
    {
      if (ops[i].norm_path)   free (ops[i].norm_path);
      if (ops[i].new_content) free (ops[i].new_content);
    }
  free (ops);
}

static bool
env_file_mentions_any_kv (const char *display_path, EnvDialect_t dialect,
                          const EnvKV_t *kvs, size_t kv_count)
{
  char *norm = xy_normalize_path (display_path);
  if (!xy_file_exist (norm))
    {
      free (norm);
      return false;
    }

  char  *content = NULL;
  size_t len     = 0;
  if (!env_read_file_bytes (norm, &content, &len))
    {
      free (norm);
      return false;
    }
  free (norm);

  bool found = false;
  size_t pos = 0;
  while (pos < len && !found)
    {
      const char *nl = memchr (content + pos, '\n', len - pos);
      size_t raw_len = nl ? (size_t) (nl - (content + pos) + 1) : (len - pos);
      size_t body_len = raw_len;
      if (body_len > 0 && '\n' == content[pos + body_len - 1])
        {
          body_len--;
          if (body_len > 0 && '\r' == content[pos + body_len - 1])
            body_len--;
        }

      for (size_t k = 0; k < kv_count; k++)
        {
          if (EnvLine_NoMatch != env_classify_line (content + pos, body_len, kvs[k].name, dialect, NULL))
            {
              found = true;
              break;
            }
        }
      pos += raw_len;
    }

  free (content);
  return found;
}

static EnvOp_t
env_apply_posix (const EnvKV_t *kvs, size_t kv_count, PosixShell_t *out_shell, char **out_err)
{
  if (out_shell) *out_shell = PosixShell_Unknown;

  /* SystemScope -> /etc/environment */
  if (chsrc_in_system_scope_mode ())
    {
      const char *etc_path = Env_Test_Etc_Env_Path ? Env_Test_Etc_Env_Path : "/etc/environment";

      if (!Env_Test_Etc_Env_Path && !chsrc_is_root () && !in_dry_run_mode ())
        {
          if (out_err)
            {
              *out_err = ENGLISH
                ? xy_strdup ("Use sudo before the command or switch to root to ensure the necessary permissions")
                : xy_strdup ("请在命令前使用 sudo 或切换为root用户来保证必要的权限");
            }
          return Env_NoPermission;
        }

      for (size_t k = 0; k < kv_count; k++)
        {
          if (!env_validate_value (kvs[k].value, EnvDialect_EtcEnv, NULL))
            {
              if (out_err)
                {
                  *out_err = ENGLISH
                    ? xy_strcat (3, "Value for `", kvs[k].name,
                                 "` contains unsafe characters (whitespace, quotes, `#`, `$`, `` ` ``, `\\`, or newline) for /etc/environment")
                    : xy_strcat (3, "变量 `", kvs[k].name,
                                 "` 的值含有 /etc/environment 不安全字符（空白符、引号、`#`、`$`、反引号、反斜杠或换行）");
                }
              return Env_BadValue;
            }
        }

      char  *norm_path   = xy_normalize_path (etc_path);
      bool   existed     = xy_file_exist (norm_path);
      char  *old_content = NULL;
      size_t old_len     = 0;

      if (existed)
        {
          if (!env_read_file_bytes (norm_path, &old_content, &old_len))
            {
              if (out_err)
                {
                  *out_err = ENGLISH ? xy_2strcat ("Unable to read file: ", norm_path)
                                     : xy_2strcat ("无法读取文件: ", norm_path);
                }
              free (norm_path);
              return Env_TargetUnwritable;
            }
        }
      else
        {
          old_content = xy_strdup ("");
          old_len     = 0;
        }

      char  *new_content = NULL;
      size_t new_len     = 0;
      bool   changed     = false;

      EnvOp_t tr = env_transform_content (old_content, old_len, EnvDialect_EtcEnv,
                                          kvs, kv_count, etc_path,
                                          &new_content, &new_len, &changed, out_err);
      free (old_content);
      if (Env_OK != tr)
        {
          free (norm_path);
          return tr;
        }

      if (!changed || (!existed && 0 == new_len))
        {
          free (new_content);
          free (norm_path);
          return Env_Unchanged;
        }

      if (existed)
        chsrc_backup (etc_path);

      if (in_dry_run_mode ())
        {
          log_for_file_write (etc_path, true);
        }
      else if (!chsrc_try_overwrite_file (new_content, etc_path))
        {
          if (out_err)
            {
              *out_err = ENGLISH ? xy_2strcat ("Unable to open or write file: ", norm_path)
                                 : xy_2strcat ("无法打开或写入文件: ", norm_path);
            }
          free (new_content);
          free (norm_path);
          return Env_TargetUnwritable;
        }

      free (new_content);
      free (norm_path);
      return Env_OK;
    }

  /* UserScope */
  PosixShell_t shell = env_detect_posix_shell ();
  if (PosixShell_Unknown == shell)
    {
      if (out_err)
        *out_err = env_build_unknown_shell_error (kvs, kv_count);
      return Env_AmbiguousTarget;
    }
  if (out_shell) *out_shell = shell;

  EnvDialect_t user_dialect = (PosixShell_Fish == shell) ? EnvDialect_Fish : EnvDialect_PosixShell;
  for (size_t k = 0; k < kv_count; k++)
    {
      if (!env_validate_value (kvs[k].value, user_dialect, NULL))
        {
          if (out_err)
            {
              *out_err = ENGLISH
                ? xy_strcat (3, "Value for `", kvs[k].name,
                             "` contains unsafe shell characters (`\"`, `\\`, `$`, `` ` ``, or newline)")
                : xy_strcat (3, "变量 `", kvs[k].name,
                             "` 的值含有不安全字符（`\"`、`\\`、`$`、反引号或换行）");
            }
          return Env_BadValue;
        }
    }

  PosixFileSpec_t write_specs[4];
  size_t          write_count = 0;
  PosixFileSpec_t check_specs[5];
  size_t          check_count = 0;

  if (PosixShell_Bash == shell)
    {
      write_specs[write_count++] = (PosixFileSpec_t) { "~/.bashrc",       EnvDialect_PosixShell, false };
      write_specs[write_count++] = (PosixFileSpec_t) { "~/.bash_profile", EnvDialect_PosixShell, false };
      write_specs[write_count++] = (PosixFileSpec_t) { "~/.profile",      EnvDialect_PosixShell, true  };

      check_specs[check_count++] = (PosixFileSpec_t) { "~/.zshrc",                    EnvDialect_PosixShell, false };
      check_specs[check_count++] = (PosixFileSpec_t) { "~/.zprofile",                 EnvDialect_PosixShell, false };
      check_specs[check_count++] = (PosixFileSpec_t) { "~/.config/fish/conf.d/chsrc.fish", EnvDialect_Fish,  false };
      check_specs[check_count++] = (PosixFileSpec_t) { "~/.config/fish/config.fish",  EnvDialect_Fish,       false };
    }
  else if (PosixShell_Zsh == shell)
    {
      write_specs[write_count++] = (PosixFileSpec_t) { "~/.zshrc",    EnvDialect_PosixShell, false };
      write_specs[write_count++] = (PosixFileSpec_t) { "~/.zprofile", EnvDialect_PosixShell, false };
      write_specs[write_count++] = (PosixFileSpec_t) { "~/.profile",  EnvDialect_PosixShell, true  };

      check_specs[check_count++] = (PosixFileSpec_t) { "~/.bashrc",                   EnvDialect_PosixShell, false };
      check_specs[check_count++] = (PosixFileSpec_t) { "~/.bash_profile",             EnvDialect_PosixShell, false };
      check_specs[check_count++] = (PosixFileSpec_t) { "~/.config/fish/conf.d/chsrc.fish", EnvDialect_Fish,  false };
      check_specs[check_count++] = (PosixFileSpec_t) { "~/.config/fish/config.fish",  EnvDialect_Fish,       false };
    }
  else if (PosixShell_Fish == shell)
    {
      write_specs[write_count++] = (PosixFileSpec_t) { "~/.config/fish/conf.d/chsrc.fish", EnvDialect_Fish, true };

      check_specs[check_count++] = (PosixFileSpec_t) { "~/.bashrc",   EnvDialect_PosixShell, false };
      check_specs[check_count++] = (PosixFileSpec_t) { "~/.zshrc",    EnvDialect_PosixShell, false };
      check_specs[check_count++] = (PosixFileSpec_t) { "~/.profile",  EnvDialect_PosixShell, false };
    }

  /* 阶段 1：全部读取并在内存完成转换校验，任一文件遇复杂语法则零落盘中止 */
  PreparedFileOp_t *ops = xy_malloc0 ((write_count + 1) * sizeof (PreparedFileOp_t));
  size_t op_count = 0;

  for (size_t i = 0; i < write_count; i++)
    {
      char *norm = xy_normalize_path (write_specs[i].display_path);
      bool existed = xy_file_exist (norm);

      if (!existed && !write_specs[i].create_if_missing)
        {
          free (norm);
          continue;
        }

      char  *old_content = NULL;
      size_t old_len     = 0;
      if (existed)
        {
          if (!env_read_file_bytes (norm, &old_content, &old_len))
            {
              if (out_err)
                {
                  *out_err = ENGLISH ? xy_2strcat ("Unable to read file: ", norm)
                                     : xy_2strcat ("无法读取文件: ", norm);
                }
              free (norm);
              env_free_prepared_ops (ops, op_count);
              return Env_TargetUnwritable;
            }
        }
      else
        {
          old_content = xy_strdup ("");
          old_len     = 0;
        }

      char  *new_content = NULL;
      size_t new_len     = 0;
      bool   changed     = false;

      EnvOp_t tr = env_transform_content (old_content, old_len, write_specs[i].dialect,
                                          kvs, kv_count, write_specs[i].display_path,
                                          &new_content, &new_len, &changed, out_err);
      free (old_content);
      if (Env_OK != tr)
        {
          free (norm);
          env_free_prepared_ops (ops, op_count);
          return tr;
        }

      /* 若文件原本不存在且转换后仍为空（例如仅做删除操作），不创建空文件 */
      if (!existed && 0 == new_len)
        {
          changed = false;
        }

      ops[op_count].display_path = write_specs[i].display_path;
      ops[op_count].norm_path    = norm;
      ops[op_count].existed      = existed;
      ops[op_count].changed      = changed;
      ops[op_count].new_content  = new_content;
      op_count++;
    }

  /* Fish 一次性迁移清理：从 ~/.config/fish/config.fish 精确移除 5 个迁移 dish 的历史同名行 */
  EnvKV_t *non_legacy_fish_kvs = NULL;
  size_t   non_legacy_count    = 0;

  if (PosixShell_Fish == shell)
    {
      EnvKV_t *cleanup_kvs   = xy_malloc0 ((kv_count > 0 ? kv_count : 1) * sizeof (EnvKV_t));
      size_t   cleanup_count = 0;
      non_legacy_fish_kvs    = xy_malloc0 ((kv_count > 0 ? kv_count : 1) * sizeof (EnvKV_t));

      for (size_t k = 0; k < kv_count; k++)
        {
          if (env_is_migrated_legacy_fish_var (kvs[k].name))
            {
              cleanup_kvs[cleanup_count++] = (EnvKV_t) { kvs[k].name, NULL };
            }
          else
            {
              non_legacy_fish_kvs[non_legacy_count++] = kvs[k];
            }
        }

      if (cleanup_count > 0)
        {
          const char *fishrc_disp = "~/.config/fish/config.fish";
          char       *fishrc_norm = xy_normalize_path (fishrc_disp);
          if (xy_file_exist (fishrc_norm))
            {
              char  *old_content = NULL;
              size_t old_len     = 0;
              if (!env_read_file_bytes (fishrc_norm, &old_content, &old_len))
                {
                  if (out_err)
                    {
                      *out_err = ENGLISH ? xy_2strcat ("Unable to read file: ", fishrc_norm)
                                         : xy_2strcat ("无法读取文件: ", fishrc_norm);
                    }
                  free (fishrc_norm);
                  free (cleanup_kvs);
                  free (non_legacy_fish_kvs);
                  env_free_prepared_ops (ops, op_count);
                  return Env_TargetUnwritable;
                }

              char  *new_content = NULL;
              size_t new_len     = 0;
              bool   changed     = false;

              EnvOp_t tr = env_transform_content (old_content, old_len, EnvDialect_Fish,
                                                  cleanup_kvs, cleanup_count, fishrc_disp,
                                                  &new_content, &new_len, &changed, out_err);
              free (old_content);
              if (Env_OK != tr)
                {
                  free (fishrc_norm);
                  free (cleanup_kvs);
                  free (non_legacy_fish_kvs);
                  env_free_prepared_ops (ops, op_count);
                  return tr;
                }

              ops[op_count].display_path = fishrc_disp;
              ops[op_count].norm_path    = fishrc_norm;
              ops[op_count].existed      = true;
              ops[op_count].changed      = changed;
              ops[op_count].new_content  = new_content;
              op_count++;
            }
          else
            {
              free (fishrc_norm);
            }
        }
      free (cleanup_kvs);
    }

  bool any_changed = false;
  for (size_t i = 0; i < op_count; i++)
    {
      if (ops[i].changed)
        {
          any_changed = true;
          break;
        }
    }

  if (!any_changed)
    {
      if (non_legacy_fish_kvs) free (non_legacy_fish_kvs);
      env_free_prepared_ops (ops, op_count);
      return Env_Unchanged;
    }

  /* 阶段 2：仅对内容实际发生变更的文件执行备份与写入 */
  for (size_t i = 0; i < op_count; i++)
    {
      if (!ops[i].changed)
        continue;

      if (ops[i].existed)
        chsrc_backup (ops[i].display_path);

      if (in_dry_run_mode ())
        {
          log_for_file_write (ops[i].display_path, true);
        }
      else if (!chsrc_try_overwrite_file (ops[i].new_content, ops[i].display_path))
        {
          if (out_err)
            {
              *out_err = ENGLISH ? xy_2strcat ("Unable to open or write file: ", ops[i].norm_path)
                                 : xy_2strcat ("无法打开或写入文件: ", ops[i].norm_path);
            }
          if (non_legacy_fish_kvs) free (non_legacy_fish_kvs);
          env_free_prepared_ops (ops, op_count);
          return Env_TargetUnwritable;
        }
    }

  /* 检查集提醒：若其他 Shell 配置文件中也定义了相同变量，仅提示不越界修改 */
  for (size_t i = 0; i < check_count; i++)
    {
      if (env_file_mentions_any_kv (check_specs[i].display_path, check_specs[i].dialect, kvs, kv_count))
        {
          char *msg = ENGLISH
            ? xy_strcat (3, "Detected matching variable definition in ", check_specs[i].display_path,
                         "; switch to that shell if you also want chsrc to update it")
            : xy_strcat (3, "检测到 ", check_specs[i].display_path,
                         " 中也存在同名环境变量定义，若您同时使用该 Shell 可切换后同步更新");
          chsrc_note2 (msg);
          free (msg);
        }
    }

  if (PosixShell_Fish == shell && non_legacy_count > 0)
    {
      if (env_file_mentions_any_kv ("~/.config/fish/config.fish", EnvDialect_Fish,
                                    non_legacy_fish_kvs, non_legacy_count))
        {
          char *msg = ENGLISH
            ? xy_strdup ("Detected matching variable definition in ~/.config/fish/config.fish; verify whether it overrides conf.d/chsrc.fish")
            : xy_strdup ("检测到 ~/.config/fish/config.fish 中也存在同名环境变量定义，请留意其加载顺序是否覆盖 conf.d/chsrc.fish");
          chsrc_note2 (msg);
          free (msg);
        }
    }

  if (non_legacy_fish_kvs) free (non_legacy_fish_kvs);
  env_free_prepared_ops (ops, op_count);
  return Env_OK;
}

/**
 * 在单个文件中查询持久化变量值（文件内多次安全赋值以最后一次为准，与 Shell 顺序执行一致）
 */
static EnvOp_t
env_query_single_posix_file (const char *display_path, EnvDialect_t dialect,
                             const char *name, bool *out_found, char **out_val,
                             char **out_err)
{
  *out_found = false;
  *out_val   = NULL;

  char *norm = xy_normalize_path (display_path);
  if (!xy_file_exist (norm))
    {
      free (norm);
      return Env_OK;
    }

  char  *content = NULL;
  size_t len     = 0;
  if (!env_read_file_bytes (norm, &content, &len))
    {
      if (out_err)
        {
          *out_err = ENGLISH ? xy_2strcat ("Unable to read file: ", norm)
                             : xy_2strcat ("无法读取文件: ", norm);
        }
      free (norm);
      return Env_TargetUnwritable;
    }
  free (norm);

  size_t pos    = 0;
  size_t lineno = 0;

  while (pos < len)
    {
      lineno++;
      const char *nl = memchr (content + pos, '\n', len - pos);
      size_t raw_len = nl ? (size_t) (nl - (content + pos) + 1) : (len - pos);
      size_t body_len = raw_len;
      if (body_len > 0 && '\n' == content[pos + body_len - 1])
        {
          body_len--;
          if (body_len > 0 && '\r' == content[pos + body_len - 1])
            body_len--;
        }

      char *parsed = NULL;
      EnvLineMatch_t m = env_classify_line (content + pos, body_len, name, dialect, &parsed);
      if (EnvLine_ComplexMatch == m)
        {
          char *line_str = xy_int2str ((int) lineno);
          if (out_err)
            {
              *out_err = ENGLISH
                ? xy_strcat (6, "Complex or dynamic syntax for `", name,
                             "` in ", display_path, ":", line_str)
                : xy_strcat (6, "配置文件 ", display_path, ":", line_str,
                             " 处的变量 `", name, "` 含有动态展开或复杂语法，无法精确确认持久化值");
            }
          free (line_str);
          free (content);
          if (*out_val)
            {
              free (*out_val);
              *out_val   = NULL;
              *out_found = false;
            }
          return Env_AmbiguousTarget;
        }
      else if (EnvLine_SafeMatch == m)
        {
          if (*out_val) free (*out_val);
          *out_val   = parsed;
          *out_found = true;
        }

      pos += raw_len;
    }

  free (content);
  return Env_OK;
}

static EnvOp_t
env_query_persisted_posix (const char *name, bool *out_found, char **out_val, char **out_err)
{
  *out_found = false;
  *out_val   = NULL;

  if (chsrc_in_system_scope_mode ())
    {
      const char *etc_path = Env_Test_Etc_Env_Path ? Env_Test_Etc_Env_Path : "/etc/environment";
      return env_query_single_posix_file (etc_path, EnvDialect_EtcEnv, name, out_found, out_val, out_err);
    }

  PosixShell_t shell = env_detect_posix_shell ();
  if (PosixShell_Unknown == shell)
    {
      if (out_err)
        *out_err = env_build_unknown_shell_error (NULL, 0);
      return Env_AmbiguousTarget;
    }

  PosixFileSpec_t specs[4];
  size_t          spec_count = 0;

  if (PosixShell_Bash == shell)
    {
      specs[spec_count++] = (PosixFileSpec_t) { "~/.bashrc",       EnvDialect_PosixShell, false };
      specs[spec_count++] = (PosixFileSpec_t) { "~/.bash_profile", EnvDialect_PosixShell, false };
      specs[spec_count++] = (PosixFileSpec_t) { "~/.profile",      EnvDialect_PosixShell, false };
    }
  else if (PosixShell_Zsh == shell)
    {
      specs[spec_count++] = (PosixFileSpec_t) { "~/.zshrc",    EnvDialect_PosixShell, false };
      specs[spec_count++] = (PosixFileSpec_t) { "~/.zprofile", EnvDialect_PosixShell, false };
      specs[spec_count++] = (PosixFileSpec_t) { "~/.profile",  EnvDialect_PosixShell, false };
    }
  else if (PosixShell_Fish == shell)
    {
      specs[spec_count++] = (PosixFileSpec_t) { "~/.config/fish/conf.d/chsrc.fish", EnvDialect_Fish, false };
      specs[spec_count++] = (PosixFileSpec_t) { "~/.config/fish/config.fish",       EnvDialect_Fish, false };
    }

  const char *first_file = NULL;
  char       *agreed_val = NULL;
  bool        any_found  = false;

  for (size_t i = 0; i < spec_count; i++)
    {
      bool  file_found = false;
      char *file_val   = NULL;

      EnvOp_t rc = env_query_single_posix_file (specs[i].display_path, specs[i].dialect,
                                                name, &file_found, &file_val, out_err);
      if (Env_OK != rc)
        {
          if (agreed_val) free (agreed_val);
          return rc;
        }

      if (!file_found)
        continue;

      if (!any_found)
        {
          any_found  = true;
          agreed_val = file_val;
          first_file = specs[i].display_path;
        }
      else
        {
          if (!xy_streql (agreed_val, file_val))
            {
              if (out_err)
                {
                  *out_err = ENGLISH
                    ? xy_strcat (9, "Conflicting values for `", name, "` between ",
                                 first_file, " (`", agreed_val, "`) and ",
                                 specs[i].display_path, " — please resolve the conflict manually")
                    : xy_strcat (9, "变量 `", name, "` 在 ",
                                 first_file, " (`", agreed_val, "`) 与 ",
                                 specs[i].display_path, " 中存在不同值冲突，请先手工统一后再查看");
                }
              free (agreed_val);
              free (file_val);
              return Env_AmbiguousTarget;
            }
          free (file_val);
        }
    }

  *out_found = any_found;
  *out_val   = agreed_val;
  return Env_OK;
}

#endif /* !XY_Build_On_Windows */


/* ============================================================
 * 跨平台统一内部入口（供公开 API 与 test/fw.c 共用）
 * ============================================================ */

static EnvOp_t
env_apply_kvs (const EnvKV_t *raw_kvs, size_t raw_count,
               PosixShell_t *out_shell, char **out_err)
{
  if (out_err)   *out_err   = NULL;
  if (out_shell) *out_shell = PosixShell_Unknown;

  if (chsrc_in_project_scope_mode ())
    {
      if (out_err)
        {
          *out_err = ENGLISH
            ? xy_strdup ("Project scope is not supported for environment variable recipes")
            : xy_strdup ("环境变量换源不支持项目级作用域 (ProjectScope)");
        }
      return Env_ScopeUnsupported;
    }

  if (0 == raw_count || NULL == raw_kvs)
    return Env_Unchanged;

  /* 1. 校验所有变量名 */
  for (size_t i = 0; i < raw_count; i++)
    {
      if (!env_validate_name (raw_kvs[i].name))
        {
          const char *bad = raw_kvs[i].name ? raw_kvs[i].name : "(null)";
          if (out_err)
            {
              *out_err = ENGLISH
                ? xy_strcat (3, "Invalid environment variable name: `", bad, "`")
                : xy_strcat (3, "非法的环境变量名: `", bad, "`");
            }
          return Env_BadName;
        }
    }

  /* 2. 去重：同名变量保留最后一次指定的值，保持首次出现的顺序 */
  EnvKV_t *kvs      = xy_malloc0 (raw_count * sizeof (EnvKV_t));
  size_t   kv_count = 0;

  for (size_t i = 0; i < raw_count; i++)
    {
      bool updated = false;
      for (size_t j = 0; j < kv_count; j++)
        {
          if (xy_streql (kvs[j].name, raw_kvs[i].name))
            {
              kvs[j].value = raw_kvs[i].value;
              updated      = true;
              break;
            }
        }
      if (!updated)
        {
          kvs[kv_count++] = raw_kvs[i];
        }
    }

#ifdef XY_Build_On_Windows
  /* Windows 注册表后端在 Phase P2 中完整实现 */
  free (kvs);
  if (out_err)
    *out_err = xy_strdup ("Windows Registry backend pending Phase P2");
  return Env_TargetUnwritable;
#else
  EnvOp_t rc = env_apply_posix (kvs, kv_count, out_shell, out_err);
  free (kvs);
  return rc;
#endif
}

static EnvOp_t
env_query_persisted (const char *name, bool *out_found, char **out_val, char **out_err)
{
  if (out_err)   *out_err   = NULL;
  if (out_found) *out_found = false;
  if (out_val)   *out_val   = NULL;

  if (chsrc_in_project_scope_mode ())
    {
      if (out_err)
        {
          *out_err = ENGLISH
            ? xy_strdup ("Project scope is not supported for environment variable recipes")
            : xy_strdup ("环境变量换源不支持项目级作用域 (ProjectScope)");
        }
      return Env_ScopeUnsupported;
    }

  if (!env_validate_name (name))
    {
      const char *bad = name ? name : "(null)";
      if (out_err)
        {
          *out_err = ENGLISH
            ? xy_strcat (3, "Invalid environment variable name: `", bad, "`")
            : xy_strcat (3, "非法的环境变量名: `", bad, "`");
        }
      return Env_BadName;
    }

#ifdef XY_Build_On_Windows
  if (out_err)
    *out_err = xy_strdup ("Windows Registry backend pending Phase P2");
  return Env_TargetUnwritable;
#else
  return env_query_persisted_posix (name, out_found, out_val, out_err);
#endif
}

static void
env_emit_post_write_hint (PosixShell_t shell)
{
  if (in_dry_run_mode ())
    return;

#ifdef XY_Build_On_Windows
  (void) shell;
  char *msg = ENGLISH
    ? "Environment variables updated in Windows Registry. Please restart your terminal for changes to take effect."
    : "已更新 Windows 注册表环境变量，请重启终端使新环境变量生效";
  chsrc_note2 (msg);
#else
  if (chsrc_in_system_scope_mode ())
    {
      char *msg = ENGLISH
        ? "Updated /etc/environment. Please re-login or start a new session for system-wide changes to take effect."
        : "已更新 /etc/environment，请重新登录系统或开启新会话使全局环境变量生效";
      chsrc_note2 (msg);
      return;
    }

  const char *reload_cmd = "source ~/.profile";
  if (PosixShell_Bash == shell)
    {
      reload_cmd = xy_file_exist ("~/.bashrc") ? "source ~/.bashrc" : "source ~/.profile";
    }
  else if (PosixShell_Zsh == shell)
    {
      reload_cmd = xy_file_exist ("~/.zshrc") ? "source ~/.zshrc" : "source ~/.profile";
    }
  else if (PosixShell_Fish == shell)
    {
      reload_cmd = "source ~/.config/fish/conf.d/chsrc.fish";
    }

  char *msg = ENGLISH
    ? xy_strcat (3, "Please restart your terminal or run `", reload_cmd, "` to apply environment variable changes immediately")
    : xy_strcat (3, "请重启终端或执行 `", reload_cmd, "` 以立即生效环境变量更改");
  chsrc_note2 (msg);
  free (msg);
#endif
}

static void
env_abort_on_error (EnvOp_t op, char *err_msg)
{
  if (err_msg)
    {
      chsrc_error2 (err_msg);
      free (err_msg);
    }
  else
    {
      chsrc_error2 (ENGLISH ? "Environment variable operation failed" : "环境变量操作失败");
    }

  if (Env_ScopeUnsupported == op)
    exit (Exit_Unsupported);
  else
    exit (Exit_UserCause);
}


/* ============================================================
 * Recipe 公开 API
 * ============================================================ */

/**
 * 读取当前进程环境变量（getenv 薄封装，未设置返回 NULL，return caller-free）
 */
static char *
chsrc_get_env (const char *name)
{
  if (!name || '\0' == name[0])
    return NULL;

  const char *val = getenv (name);
  return val ? xy_strdup (val) : NULL;
}

/**
 * 单变量持久化写入或删除（value == NULL 表示删除）
 */
static EnvOp_t
chsrc_set_env (const char *name, const char *value)
{
  EnvKV_t      kv    = { name, value };
  PosixShell_t shell = PosixShell_Unknown;
  char        *err   = NULL;

  EnvOp_t op = env_apply_kvs (&kv, 1, &shell, &err);
  if (Env_OK == op)
    {
      if (err) free (err);
      env_emit_post_write_hint (shell);
      return Env_OK;
    }
  if (Env_Unchanged == op)
    {
      if (err) free (err);
      return Env_Unchanged;
    }

  env_abort_on_error (op, err);
  return op;
}

/**
 * 多变量原子持久化写入或删除：以首个 NULL name 结束的 (name, value) 交替变参表
 */
static EnvOp_t
chsrc_set_env_block (const char *name1, ...)
{
  if (NULL == name1)
    return Env_Unchanged;

  size_t   cap   = 8;
  size_t   count = 0;
  EnvKV_t *kvs   = xy_malloc0 (cap * sizeof (EnvKV_t));

  va_list ap;
  va_start (ap, name1);

  const char *cur_name = name1;
  while (NULL != cur_name)
    {
      const char *cur_val = va_arg (ap, const char *);
      if (count >= cap)
        {
          cap *= 2;
          EnvKV_t *grown = realloc (kvs, cap * sizeof (EnvKV_t));
          if (!grown)
            {
              va_end (ap);
              free (kvs);
              xy_throw ("Memory allocation failed in chsrc_set_env_block");
            }
          kvs = grown;
        }
      kvs[count++] = (EnvKV_t) { cur_name, cur_val };
      cur_name     = va_arg (ap, const char *);
    }

  va_end (ap);

  PosixShell_t shell = PosixShell_Unknown;
  char        *err   = NULL;

  EnvOp_t op = env_apply_kvs (kvs, count, &shell, &err);
  free (kvs);

  if (Env_OK == op)
    {
      if (err) free (err);
      env_emit_post_write_hint (shell);
      return Env_OK;
    }
  if (Env_Unchanged == op)
    {
      if (err) free (err);
      return Env_Unchanged;
    }

  env_abort_on_error (op, err);
  return op;
}

/**
 * 查看持久化环境变量（以 NULL 结尾的变量名变参表），若当前进程值与之不同则附带提示
 */
static void
chsrc_view_env (const char *var1, ...)
{
  if (!var1)
    {
      xy_unreached ();
      return;
    }

  size_t       cap   = 8;
  size_t       count = 0;
  const char **vars  = xy_malloc0 (cap * sizeof (const char *));

  va_list ap;
  va_start (ap, var1);
  const char *cur = var1;
  while (cur)
    {
      if (count >= cap)
        {
          cap *= 2;
          const char **grown = realloc (vars, cap * sizeof (const char *));
          if (!grown)
            {
              va_end (ap);
              free (vars);
              xy_throw ("Memory allocation failed in chsrc_view_env");
            }
          vars = grown;
        }
      vars[count++] = cur;
      cur = va_arg (ap, const char *);
    }
  va_end (ap);

  /* 先对全部待查变量完成持久化读取校验，遇到未知 Shell / 冲突 / 复杂语法立即中止，避免输出半截结果 */
  bool  *founds = xy_malloc0 (count * sizeof (bool));
  char **vals   = xy_malloc0 (count * sizeof (char *));

  for (size_t i = 0; i < count; i++)
    {
      char   *err = NULL;
      EnvOp_t rc  = env_query_persisted (vars[i], &founds[i], &vals[i], &err);
      if (Env_OK != rc)
        {
          for (size_t j = 0; j < i; j++)
            {
              if (vals[j]) free (vals[j]);
            }
          free (founds);
          free (vals);
          free (vars);
          env_abort_on_error (rc, err);
          return;
        }
      if (err) free (err);
    }

  for (size_t i = 0; i < count; i++)
    {
      const char *persisted = founds[i] ? vals[i] : "";
      printf ("%s=%s\n", vars[i], persisted);

      const char *proc_val = getenv (vars[i]);
      if (founds[i])
        {
          if (proc_val && !xy_streql (vals[i], proc_val))
            {
              char *note = ENGLISH
                ? xy_strcat (5, "Current terminal session has a different value for `", vars[i],
                             "` (`", proc_val, "`); restart terminal or reload shell profile to sync")
                : xy_strcat (5, "当前终端会话中的 `", vars[i],
                             "` 值为 `", proc_val, "`（与已持久化配置不同），重启终端或重新加载配置文件后同步");
              chsrc_note2 (note);
              free (note);
            }
          else if (!proc_val)
            {
              char *note = ENGLISH
                ? xy_strcat (3, "Current terminal session has not loaded `", vars[i],
                             "` yet; restart terminal or reload shell profile to take effect")
                : xy_strcat (3, "当前终端会话尚未加载 `", vars[i],
                             "`，重启终端或重新加载配置文件后生效");
              chsrc_note2 (note);
              free (note);
            }
        }
      else if (proc_val && '\0' != proc_val[0])
        {
          char *note = ENGLISH
            ? xy_strcat (5, "`", vars[i],
                         "` is unset in persistent config, but current session still has `", proc_val, "`")
            : xy_strcat (5, "持久化配置中未设置 `", vars[i],
                         "`，但当前终端会话中仍存在临时或旧值 `", proc_val, "`");
          chsrc_note2 (note);
          free (note);
        }

      if (vals[i]) free (vals[i]);
    }

  free (founds);
  free (vals);
  free (vars);
}
