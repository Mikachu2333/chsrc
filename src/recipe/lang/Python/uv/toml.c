/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * -------------------------------------------------------------
 * File Authors  : @Mikachu2333
 * Contributors  : @ccmywish
 * Created On    : <2026-08-02>
 * Last Modified : <2026-08-13>
 *
 * uv 配置 (uv.toml / pyproject.toml) 改写工具。
 *
 * TOML 通用的行定位、字符串处理与文本改写函数位于 lib/mikachu_toml.h;
 * 本文件只保留 uv 配置语义。
 * ------------------------------------------------------------*/

/**
 * @brief 查找要管理的 index 段。
 *
 * 多个 index 中优先选择 default = true 的段, 不存在 default 时回退到第一个。
 */
static const char *
pl_uv_toml_find_managed_index (const char *content, const char *index_header)
{
  const char *first = NULL;
  for (const char *s = content; *s; s = mktoml_next_line (s))
    {
      if (!mktoml_header_match (s, index_header)) continue;
      if (!first) first = s;

      const char *body = mktoml_next_line (s);
      const char *end = mktoml_find_section_end (body);
      const char *default_line = mktoml_find_key_in_section (body, end, "default");
      if (default_line && mktoml_value_is_true (default_line)) return s;
    }
  return first;
}

/**
 * @brief 获取所管理 index 段的 url。
 */
static char *
pl_uv_toml_get_index_url (const char *content, const char *index_header)
{
  const char *index = pl_uv_toml_find_managed_index (content, index_header);
  if (!index) return NULL;

  const char *first = mktoml_next_line (index);
  const char *end = mktoml_find_section_end (first);
  const char *url_line = mktoml_find_key_in_section (first, end, "url");
  if (!url_line) return NULL;

  return mktoml_extract_string_value (url_line);
}

/**
 * @brief 替换/创建 index 数组表的 url 值。
 */
static char *
pl_uv_toml_replace_index_url (const char *content, const char *url, const char *index_header, const char *parent_table)
{
  const char *ih = pl_uv_toml_find_managed_index (content, index_header);
  const char *boundary = content + strlen (content);
  if (!ih && parent_table)
    {
      const char *table = mktoml_find_table (content, parent_table);
      if (!table)
        {
          char *escaped_url = mktoml_escape_basic_string (url);
          size_t seg_len = strlen (parent_table) + strlen (index_header) + strlen (escaped_url) + 48;
          char *seg = calloc (seg_len, 1);
          snprintf (seg, seg_len, "%s\n%s\nurl = \"%s\"\ndefault = true\n",
                    parent_table, index_header, escaped_url);
          free (escaped_url);
          char *ret = mktoml_append_segment (content, seg);
          free (seg);
          return ret;
        }
    }

  if (!ih)
    {
      char *escaped_url = mktoml_escape_basic_string (url);
      size_t seglen = strlen (index_header) + strlen (escaped_url) + 48;
      char *seg = malloc (seglen);
      snprintf (seg, seglen, "%s\nurl = \"%s\"\ndefault = true\n", index_header, escaped_url);
      free (escaped_url);
      char *ret = mktoml_insert_before (content, boundary, seg);
      free (seg);
      return ret;
    }

  const char *first = mktoml_next_line (ih);
  const char *end = mktoml_find_section_end (first);
  const char *url_line = mktoml_find_key_in_section (first, end, "url");

  if (!url_line)
    {
      bool has_default = mktoml_find_key_in_section (first, end, "default") != NULL;
      char *escaped_url = mktoml_escape_basic_string (url);
      size_t seglen = strlen (escaped_url) + (has_default ? 24 : 48);
      char *seg = malloc (seglen);
      size_t spos = 0;
      spos += snprintf (seg + spos, seglen - spos, "url = \"%s\"\n", escaped_url);
      if (!has_default)
        spos += snprintf (seg + spos, seglen - spos, "default = true\n");
      char *ret = mktoml_insert_before (content, first, seg);
      free (seg);
      free (escaped_url);
      return ret;
    }

  char *escaped_url = mktoml_escape_basic_string (url);
  size_t newlen = strlen (escaped_url) + 32;
  char *new_line = malloc (newlen);
  snprintf (new_line, newlen, "url = \"%s\"", escaped_url);
  char *ret = mktoml_replace_line (content, url_line, new_line);
  free (new_line);
  free (escaped_url);
  return ret;
}

/**
 * @brief 替换/创建顶层键或 [tool.uv] 内键的字符串值。
 */
static char *
pl_uv_toml_replace_key_value (const char *content, const char *key, const char *url, const char *parent_table)
{
  const char *old_line = NULL;
  const char *insert_at = NULL;

  if (parent_table)
    {
      const char *table = mktoml_find_table (content, parent_table);
      if (!table)
        {
          char *escaped_url = mktoml_escape_basic_string (url);
          size_t seg_len = strlen (parent_table) + strlen (key) + strlen (escaped_url) + 16;
          char *seg = calloc (seg_len, 1);
          snprintf (seg, seg_len, "%s\n%s = \"%s\"\n", parent_table, key, escaped_url);
          free (escaped_url);
          char *ret = mktoml_append_segment (content, seg);
          free (seg);
          return ret;
        }

      const char *first = mktoml_next_line (table);
      const char *key_zone_end = NULL;
      for (const char *s = first; *s; s = mktoml_next_line (s))
        {
          const char *line = mktoml_skip_indent (s);
          if (*line == '[')
            {
              key_zone_end = s;
              break;
            }
        }
      old_line = mktoml_find_key_in_section (first, key_zone_end, key);
      insert_at = key_zone_end ? key_zone_end : content + strlen (content);
    }
  else
    {
      const char *p = content;
      while (*p)
        {
          if (*mktoml_skip_indent (p) == '[') { insert_at = p; break; }
          if (mktoml_key_prefix (mktoml_skip_indent (p), key)) old_line = p;
          p = mktoml_next_line (p);
        }
      if (!insert_at) insert_at = content + strlen (content);
    }

  if (old_line)
    {
      char *escaped_url = mktoml_escape_basic_string (url);
      size_t newlen = strlen (key) + strlen (escaped_url) + 32;
      char *new_line = malloc (newlen);
      snprintf (new_line, newlen, "%s = \"%s\"", key, escaped_url);
      char *ret = mktoml_replace_line (content, old_line, new_line);
      free (new_line);
      free (escaped_url);
      return ret;
    }

  char *escaped_url = mktoml_escape_basic_string (url);
  size_t seglen = strlen (key) + strlen (escaped_url) + 32;
  char *seg = malloc (seglen);
  snprintf (seg, seglen, "%s = \"%s\"\n", key, escaped_url);
  char *ret = mktoml_insert_before (content, insert_at, seg);
  free (seg);
  free (escaped_url);
  return ret;
}

/**
 * @brief 在指定表或顶层提取键的字符串值。
 */
static char *
pl_uv_toml_get_value_in_table (const char *content, const char *key, const char *parent_table)
{
  const char *p = content;
  if (parent_table)
    {
      const char *table = mktoml_find_table (content, parent_table);
      if (!table) return NULL;
      p = mktoml_next_line (table);
    }

  const char *limit = mktoml_find_section_end (p);
  for (const char *s = p; *s && (!limit || s < limit); s = mktoml_next_line (s))
    {
      if (mktoml_key_prefix (mktoml_skip_indent (s), key))
        return mktoml_extract_string_value (s);
    }
  return NULL;
}
