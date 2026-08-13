/* ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * -------------------------------------------------------------
 * Lib Authors   : Mikachu2333 <linkchou@yandex.com>
 * Contributors  : 曾奥然      <ccmywish@qq.com>
 *               |
 * Created On    : <2026-08-02>
 * Last Modified : <2026-08-13>
 *
 *
 *    mikachu-toml.h: 由 Mikachu2333 编写的简易 TOML 解析库
 *
 * 这里只实现 chsrc 当前需要的 TOML 子集，不是完整 TOML 解析器：
 * 以行定位表头和键，并支持 basic/literal string 的有限读取与写入
 * ------------------------------------------------------------*/

#ifndef MIKACHU_TOML_H
#define MIKACHU_TOML_H

static bool
mktoml_key_prefix (const char *p, const char *key)
{
  size_t kl = strlen (key);
  const char *end = NULL;
  if (strncmp (p, key, kl) == 0)
    end = p + kl;
  else if ((*p == '"' || *p == '\'')
           && strncmp (p + 1, key, kl) == 0 && p[kl + 1] == *p)
    end = p + kl + 2;
  else
    return false;

  char c = *end;
  return c == '=' || c == ' ' || c == '\t' || c == '\n' || c == '\0';
}

static const char *
mktoml_skip_indent (const char *p)
{
  while (*p == ' ' || *p == '\t') p++;
  return p;
}

static bool
mktoml_header_match (const char *p, const char *header)
{
  p = mktoml_skip_indent (p);
  size_t hl = strlen (header);
  if (strncmp (p, header, hl) == 0)
    {
      char c = p[hl];
      return c == '\n' || c == '\r' || c == '\0' || c == ' ' || c == '\t' || c == '#';
    }
  if (strcmp (header, "[tool.uv]") == 0
      && (strncmp (p, "[tool.\"uv\"]", 11) == 0
          || strncmp (p, "[tool.'uv']", 11) == 0))
    {
      char c = p[11];
      return c == '\n' || c == '\r' || c == '\0' || c == ' ' || c == '\t' || c == '#';
    }
  return false;
}

static const char *
mktoml_next_line (const char *p)
{
  while (*p && *p != '\n') p++;
  if (*p == '\n') p++;
  return p;
}

static const char *
mktoml_find_section_end (const char *start)
{
  const char *p = start;
  while (*p)
    {
      if (*mktoml_skip_indent (p) == '[') return p;
      p = mktoml_next_line (p);
    }
  return NULL;
}

static const char *
mktoml_find_table (const char *content, const char *header)
{
  const char *p = content;
  while (*p)
    {
      const char *line = mktoml_skip_indent (p);
      if (*line == '[' && mktoml_header_match (line, header)) return p;
      p = mktoml_next_line (p);
    }
  return NULL;
}

static const char *
mktoml_find_key_in_section (const char *first, const char *end, const char *key)
{
  for (const char *s = first; *s && (!end || s < end); s = mktoml_next_line (s))
    {
      if (mktoml_key_prefix (mktoml_skip_indent (s), key)) return s;
    }
  return NULL;
}

static char *
mktoml_escape_basic_string (const char *value)
{
  size_t n = 0;
  for (const char *p = value; *p; p++)
    {
      switch (*p)
        {
        case '\\': case '"': case '\b': case '\t': case '\n':
        case '\f': case '\r': n += 2; break;
        default: n++;
        }
    }

  char *ret = malloc (n + 1);
  size_t pos = 0;
  for (const char *p = value; *p; p++)
    {
      switch (*p)
        {
        case '\\': ret[pos++] = '\\'; ret[pos++] = '\\'; break;
        case '"':  ret[pos++] = '\\'; ret[pos++] = '"'; break;
        case '\b': ret[pos++] = '\\'; ret[pos++] = 'b'; break;
        case '\t': ret[pos++] = '\\'; ret[pos++] = 't'; break;
        case '\n': ret[pos++] = '\\'; ret[pos++] = 'n'; break;
        case '\f': ret[pos++] = '\\'; ret[pos++] = 'f'; break;
        case '\r': ret[pos++] = '\\'; ret[pos++] = 'r'; break;
        default: ret[pos++] = *p;
        }
    }
  ret[pos] = '\0';
  return ret;
}

static int
mktoml_hex_digit (char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool
mktoml_append_utf8 (char **out, size_t *pos, unsigned long codepoint)
{
  if (codepoint <= 0x7f)
    (*out)[(*pos)++] = (char)codepoint;
  else if (codepoint <= 0x7ff)
    {
      (*out)[(*pos)++] = (char)(0xc0 | (codepoint >> 6));
      (*out)[(*pos)++] = (char)(0x80 | (codepoint & 0x3f));
    }
  else if (codepoint <= 0xffff)
    {
      if (codepoint >= 0xd800 && codepoint <= 0xdfff) return false;
      (*out)[(*pos)++] = (char)(0xe0 | (codepoint >> 12));
      (*out)[(*pos)++] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
      (*out)[(*pos)++] = (char)(0x80 | (codepoint & 0x3f));
    }
  else if (codepoint <= 0x10ffff)
    {
      (*out)[(*pos)++] = (char)(0xf0 | (codepoint >> 18));
      (*out)[(*pos)++] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
      (*out)[(*pos)++] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
      (*out)[(*pos)++] = (char)(0x80 | (codepoint & 0x3f));
    }
  else return false;
  return true;
}

static char *
mktoml_extract_string_value (const char *line)
{
  const char *eq = strchr (mktoml_skip_indent (line), '=');
  if (!eq) return NULL;
  const char *v = eq + 1;
  while (*v == ' ' || *v == '\t') v++;
  if (*v != '"' && *v != '\'') return NULL;
  char quote = *v++;

  size_t max_len = strcspn (v, "\n") + 1;
  char *ret = malloc (max_len * 4);
  size_t pos = 0;
  bool closed = false;
  while (*v && *v != '\n')
    {
      if (*v == quote)
        {
          closed = true;
          break;
        }
      if (quote == '"' && *v == '\\')
        {
          v++;
          if (*v == 'u' || *v == 'U')
            {
              int digits = (*v == 'u') ? 4 : 8;
              unsigned long codepoint = 0;
              v++;
              for (int i = 0; i < digits; i++, v++)
                {
                  int digit = mktoml_hex_digit (*v);
                  if (digit < 0) { free (ret); return NULL; }
                  codepoint = (codepoint << 4) | (unsigned)digit;
                }
              if (!mktoml_append_utf8 (&ret, &pos, codepoint))
                { free (ret); return NULL; }
              continue;
            }
          switch (*v)
            {
            case '"': ret[pos++] = '"'; break;
            case '\\': ret[pos++] = '\\'; break;
            case 'b': ret[pos++] = '\b'; break;
            case 't': ret[pos++] = '\t'; break;
            case 'n': ret[pos++] = '\n'; break;
            case 'f': ret[pos++] = '\f'; break;
            case 'r': ret[pos++] = '\r'; break;
            default: free (ret); return NULL;
            }
          if (*v) v++;
          continue;
        }
      ret[pos++] = *v++;
    }
  if (!closed)
    {
      free (ret);
      return NULL;
    }
  ret[pos] = '\0';
  return ret;
}

static bool
mktoml_value_is_true (const char *line)
{
  const char *eq = strchr (mktoml_skip_indent (line), '=');
  if (!eq) return false;
  const char *v = mktoml_skip_indent (eq + 1);
  return strncmp (v, "true", 4) == 0
      && (v[4] == '\0' || v[4] == '\r' || v[4] == '\n' || v[4] == ' ' || v[4] == '\t' || v[4] == '#');
}

static char *
mktoml_replace_line (const char *content, const char *line, const char *new_line_text)
{
  const char *line_end = strchr (line, '\n');
  if (!line_end) line_end = content + strlen (content);

  const char *indent_end = mktoml_skip_indent (line);
  size_t ind = indent_end - line;

  size_t len = strlen (content) + ind + strlen (new_line_text) + 8;
  char *ret = calloc (len, 1);
  size_t pos = 0;
  pos += snprintf (ret + pos, len - pos, "%.*s", (int)(line - content), content);
  pos += snprintf (ret + pos, len - pos, "%.*s", (int)ind, line);
  pos += snprintf (ret + pos, len - pos, "%s", new_line_text);
  strcpy (ret + pos, line_end);
  return ret;
}

static char *
mktoml_insert_before (const char *content, const char *insert_at, const char *insert_text)
{
  size_t head = insert_at - content;
  bool need_sep = (head > 0 && content[head - 1] != '\n');

  size_t text_len = strlen (insert_text);
  size_t len = strlen (content) + text_len + (need_sep ? 1 : 0) + 16;
  char *ret = calloc (len, 1);
  size_t pos = 0;
  pos += snprintf (ret + pos, len - pos, "%.*s", (int)head, content);
  if (need_sep) ret[pos++] = '\n';
  memcpy (ret + pos, insert_text, text_len);
  pos += text_len;
  strcpy (ret + pos, content + head);
  return ret;
}

static char *
mktoml_append_segment (const char *content, const char *segment)
{
  return mktoml_insert_before (content, content + strlen (content), segment);
}

#endif
