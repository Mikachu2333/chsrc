/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_target(pl_py_uv, "uv");

#include "uv-helper.c"
#include "uv-python-build.c"

// 调整 `python-install-mirror` 相关配置应在 `uv-python-build.c` 中修改，本处仅为 uv 最常用的 `pypi` 镜像配置。
void
pl_py_uv_prelude (void)
{
  chef_prep_this (pl_py_uv, gsr);

  chef_set_recipe_created_on   (this, "2024-12-11");
  chef_set_recipe_last_updated (this, "2026-08-11");

  chef_set_chefs (this, 3, "@happy-game", "@MingriLingran", "@Mikachu2333");
  chef_set_sauciers (this, 2, "@Kattos", "@ccmywish");

  chef_set_scope_cap (this, ProjectScope, ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, UserScope,    ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, SystemScope,  ScopeCap_Able_But_Not_Implemented);
  chef_set_default_scope (this, UserScope);

  chef_allow_english(this);
  chef_allow_user_define(this);

  chef_use_other_target_sources (this, &pl_py_pypi_target);

  // 挂载 源target
  chef_set_preludefn_for_sources_target (pl_py_uv_python_build);
}


/**
 * @consult: https://docs.astral.sh/uv/concepts/configuration-files
 * chsrc get uv
 *
 * uv 配置文件选择顺序 (chsrc 写入策略)：
 * 1. ./uv.toml        (项目级；与 pyproject.toml 并存时 uv 优先读取它)
 * 2. ./pyproject.toml (项目级 [tool.uv] 段)
 * 3. 用户级 uv.toml（Unix 下遵循 XDG_CONFIG_HOME）
 *
 * uv 还会向父目录查找最近的项目配置，并合并用户/系统配置；chsrc 为避免
 * 越界修改父项目，只管理当前目录或明确的用户级配置文件。
 * Unix 用户配置目录优先使用 $XDG_CONFIG_HOME/uv，未设置时回退到 ~/.config/uv。
 */

#define pl_py_uv_ConfigFile          "uv.toml"
#define pl_py_uv_PyprojectConfigFile "pyproject.toml"
// 注意: 不能以 "./" 开头 —— Windows 上 chsrc_backup 的 copy 命令无法处理
#define pl_py_uv_Local_ConfigPath    ""
#define pl_py_uv_User_ConfigPath     "~/.config"

static const char *
pl_py_uv_user_config_path (void)
{
  const char *xdg = getenv ("XDG_CONFIG_HOME");
  return (xdg && *xdg) ? xdg : pl_py_uv_User_ConfigPath;
}

static Source_t
pl_py_uv_yield_target_source (Target_t *target, char *option)
{
  if (!target->inited) target->preludefn ();

  if (hp_is_url (option))
    {
      Source_t user = { &UserDefinedProvider, option };
      return user;
    }

  int index = use_specific_mirror_or_auto_select (option, target);
  return target->sources[index];
}

static char *
pl_py_uv_read_config (const char *path)
{
  if (!xy_file_exist (path)) return xy_strdup ("");

  char *file = xy_normalize_path (path);
  FILE *f = fopen (file, "rb");
  if (!f)
    {
      return NULL;
    }
  fclose (f);

  /* xy_file_read 会把 CRLF 与孤立 CR 统一为 LF */
  char *content = xy_file_read (file);
  return content;
}


char *
pl_python_find_uv_config (bool mkdir)
{
  if (chsrc_in_project_scope_mode())
    {
      // uv.toml 与 pyproject.toml 并存时，uv 会忽略后者中的 [tool.uv]。
      char *uv_toml = xy_2strcat (pl_py_uv_Local_ConfigPath, pl_py_uv_ConfigFile);
      if (xy_file_exist (uv_toml))
        return uv_toml;
      free (uv_toml);

      char *pyproject = xy_2strcat (pl_py_uv_Local_ConfigPath, pl_py_uv_PyprojectConfigFile);
      if (xy_file_exist (pyproject))
        return pyproject;
      free (pyproject);
      return xy_2strcat (pl_py_uv_Local_ConfigPath, pl_py_uv_ConfigFile);
    }
  else
    {
      if (xy.on_windows)
        {
          // config path on Windows
          char *appdata = getenv ("APPDATA");

          if (!appdata)
            {
              chsrc_error2 ("未能获取 APPDATA 环境变量");
              return NULL;
            }

          char *config_dir = xy_path_join (appdata, "uv");
          if (mkdir)
            {
              chsrc_ensure_dir (config_dir);
            }
          char *result = xy_path_join (config_dir, pl_py_uv_ConfigFile);
          free (config_dir);
          return result;
        }
      else
        {
          // config path on Linux or macOS
          const char *config_path = pl_py_uv_user_config_path ();
          char *config_dir = xy_path_join (config_path, "uv");
          if (mkdir)
            {
              chsrc_ensure_dir (config_dir);
            }
          char *result = xy_path_join (config_dir, pl_py_uv_ConfigFile);
          free (config_dir);
          return result;
        }
    }
}


void
pl_py_uv_getsrc (char *option)
{
  char *uv_config = pl_python_find_uv_config (false);

  if (!uv_config || !chsrc_check_file (uv_config))
    {
      if (!uv_config)
        chsrc_error2 ("无法获取 uv 配置文件路径");
      else
        chsrc_error2 ("未找到 uv 配置文件");
      free (uv_config);
      return;
    }

  // uv.toml 与 pyproject.toml 均使用同一套受限 TOML 读取逻辑。
  char *content = pl_py_uv_read_config (uv_config);
  if (!content)
    {
      chsrc_error2 ("无法读取 uv 配置文件");
      free (uv_config);
      return;
    }

  bool pyproject = xy_str_end_with (uv_config, pl_py_uv_PyprojectConfigFile);
  const char *index_header = pyproject ? "[[tool.uv.index]]" : "[[index]]";
  const char *parent_table = pyproject ? "[tool.uv]" : NULL;

  char *url = uvh_get_index_url (content, index_header);
  if (url)
    {
      say (url);
      free (url);
    }
  else
    {
      if (ENGLISH)
        chsrc_note2 ("No source configured in uv, showing default upstream source:");
      else
        chsrc_note2 ("uv 中未配置源，显示默认上游源：");
      Source_t default_source = chsrc_yield_source (&pl_py_pypi_target, "upstream");
      say (default_source.url);
    }

  char *mirror = uvh_get_value_in_table (content, "python-install-mirror", parent_table);
  if (mirror)
    {
      say (mirror);
      free (mirror);
    }

  free (content);
  free (uv_config);
}



/**
 * 一次性完成uv配置文件的全部文件操作 (set 路径)
 */
static bool
pl_py_uv_write_all (const char *uv_config, const char *pypi_url, const char *py_dl_url)
{
  char *content = pl_py_uv_read_config (uv_config);
  if (!content)
    {
      chsrc_error2 ("无法读取 uv 配置文件");
      return false;
    }

  bool pyproject = xy_str_end_with (uv_config, pl_py_uv_PyprojectConfigFile);
  const char *index_header = pyproject ? "[[tool.uv.index]]" : "[[index]]";
  const char *parent_table = pyproject ? "[tool.uv]" : NULL;

  char *updated = replace_index_url (content, pypi_url, index_header, parent_table);
  char *final = replace_key_value (updated, "python-install-mirror", py_dl_url, parent_table);
  free (updated);

  /* final 仍为纯 LF。框架最终以文本模式写入：Windows 写盘时转换为 CRLF，
   * 其他平台保持 LF。 */
  chsrc_overwrite_file (final, uv_config);
  free (final);
  free (content);
  return true;
}


/**
 * chsrc set uv
 *
 * 同时更换两部分:
 *   1. PyPI 索引源               (`[[index]]` 表)
 *   2. Python 解释器下载源       (`python-install-mirror`)
 *
 * @consult https://docs.astral.sh/uv/reference/settings/#python-install-mirror
 */
void
pl_py_uv_setsrc (char *option)
{
  chsrc_ensure_program ("uv");

  char *uv_config = pl_python_find_uv_config (true);
  if (NULL == uv_config)
    {
      chsrc_error2 ("无法获取 uv 配置文件路径");
      return;
    }

  /**
   * reset: 把 index 与 python-install-mirror 写回
   * 默认上游（PyPI 与 python-build-standalone）， 不依赖 .bak。
   */
  if (chsrc_in_reset_mode ())
    {
      if (!chsrc_check_file (uv_config))
        {
          chsrc_info ("没有 uv 配置文件，无需重置");
          free (uv_config);
          return;
        }

      Source_t default_pypi = pl_py_uv_yield_target_source (&pl_py_pypi_target, "upstream");
      Source_t default_gh   = pl_py_uv_yield_target_source (&pl_py_uv_python_build_target, "upstream");

      chsrc_backup (uv_config);
      if (!pl_py_uv_write_all (uv_config, default_pypi.url, default_gh.url))
        {
          free (uv_config);
          return;
        }
      free (uv_config);

      return;
    }

  /**
   * set: 选取源并写入。
   * 两个 URL 的语义不同：自定义 URL 只作为 PyPI index，Python 下载镜像仍自动测速。
   */
  char *pypi_opt = option;
  char *gh_opt = NULL;

  if (option && !hp_is_url (option))
    {
      if (!pl_py_uv_python_build_target.inited)
        pl_py_uv_python_build_target.preludefn ();
      bool gh_found = false;
      bool pypi_found = false;
      for (int i = 0; i < pl_py_uv_python_build_target.sources_n; i++)
        if (xy_streql (pl_py_uv_python_build_target.sources[i].mirror->code, option))
          { gh_found = true; break; }
      if (!pl_py_pypi_target.inited)
        pl_py_pypi_target.preludefn ();
      for (int i = 0; i < pl_py_pypi_target.sources_n; i++)
        if (xy_streql (pl_py_pypi_target.sources[i].mirror->code, option))
          { pypi_found = true; break; }

      if (gh_found && !pypi_found)
        {
          pypi_opt = NULL;
          gh_opt = option;
        }
      else if (gh_found)
        gh_opt = option; // 共有 code: 两个 target 都使用同一 code
    }

  Source_t source = chsrc_yield_source (&pl_py_pypi_target, pypi_opt);
  chsrc_confirm_source (&source);

  // 内部 target 不得复用 Python group leader 的数组下标。
  Source_t gh_source = pl_py_uv_yield_target_source (&pl_py_uv_python_build_target, gh_opt);
  chsrc_confirm_source (&gh_source);

  chsrc_backup (uv_config);
  if (!pl_py_uv_write_all (uv_config, source.url, gh_source.url))
    {
      return;
    }

  chsrc_determine_chgtype (ChgType_Auto);
  chsrc_conclude (&source);
}


void
pl_py_uv_resetsrc (char *option)
{
  pl_py_uv_setsrc (option);
}
