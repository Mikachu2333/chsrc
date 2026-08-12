/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_target(pl_uv_pypi_index, "uv");

#include "uv-helper.c"

void
pl_uv_pypi_index_prelude (void)
{
  chef_prep_this (pl_uv_pypi_index, gsr);

  chef_set_recipe_created_on   (this, "2024-12-11");
  chef_set_recipe_last_updated (this, "2026-08-12");

  chef_set_chefs (this, 3, "@happy-game", "@MingriLingran", "@Mikachu2333");
  chef_set_sauciers (this, 2, "@ccmywish", "@Kattos");

  chef_set_scope_cap (this, ProjectScope, ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, UserScope,    ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, SystemScope,  ScopeCap_Able_But_Not_Implemented);
  chef_set_default_scope (this, UserScope);

  chef_allow_english(this);
  chef_allow_user_define(this);

  chef_use_other_target_sources (this, &pl_py_pypi_target);

  // 挂载 源target
  chef_set_preludefn_for_sources_target (pl_uv_python_build);
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

#define PL_uv_ConfigFile          "uv.toml"
#define PL_uv_PyprojectConfigFile "pyproject.toml"
// 注意: 不能以 "./" 开头 —— Windows 上 chsrc_backup 的 copy 命令无法处理
#define PL_uv_Local_ConfigPath    ""
#define PL_uv_User_ConfigPath     "~/.config"

static const char *
pl_uv_pypi_index_user_config_path (void)
{
  const char *xdg = getenv ("XDG_CONFIG_HOME");
  return (xdg && *xdg) ? xdg : PL_uv_User_ConfigPath;
}



/**
 * @the-return-is-NULLable
 */
char *
pl_py_find_uv_config (bool mkdir)
{
  if (chsrc_in_project_scope_mode())
    {
      // uv.toml 与 pyproject.toml 并存时，uv 会忽略后者中的 [tool.uv]。
      char *uv_toml = xy_2strcat (PL_uv_Local_ConfigPath, PL_uv_ConfigFile);
      if (xy_file_exist (uv_toml))
        return uv_toml;

      char *pyproject = xy_2strcat (PL_uv_Local_ConfigPath, PL_uv_PyprojectConfigFile);
      if (xy_file_exist (pyproject))
        return pyproject;
      return xy_2strcat (PL_uv_Local_ConfigPath, PL_uv_ConfigFile);
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
          char *result = xy_path_join (config_dir, PL_uv_ConfigFile);
          return result;
        }
      else
        {
          // config path on Linux or macOS
          const char *config_path = pl_uv_pypi_index_user_config_path ();
          char *config_dir = xy_path_join (config_path, "uv");
          if (mkdir)
            {
              chsrc_ensure_dir (config_dir);
            }
          char *result = xy_path_join (config_dir, PL_uv_ConfigFile);
          return result;
        }
    }
}


void
pl_uv_pypi_index_getsrc (char *option)
{
  char *uv_config = pl_py_find_uv_config (false);

  if (!uv_config || !chsrc_check_file (uv_config))
    {
      if (!uv_config)
        chsrc_error2 ("无法获取 uv 配置文件路径");
      else
        chsrc_error2 ("未找到 uv 配置文件");
      return;
    }

  // uv.toml 与 pyproject.toml 均使用同一套受限 TOML 读取逻辑。
  char *content = xy_read_file (xy_normalize_path (uv_config));
  if (!content)
    {
      chsrc_error2 ("无法读取 uv 配置文件");
      return;
    }

  bool pyproject = xy_str_end_with (uv_config, PL_uv_PyprojectConfigFile);
  const char *index_header = pyproject ? "[[tool.uv.index]]" : "[[index]]";

  char *url = uvh_get_index_url (content, index_header);
  if (url)
    {
      println (url);
    }
  else
    {
      if (ENGLISH)
        chsrc_note2 ("No source configured in uv, showing default upstream source:");
      else
        chsrc_note2 ("uv 中未配置源，显示默认上游源：");
      Source_t default_source = chsrc_yield_source (&pl_py_pypi_target, "upstream");
      println (default_source.url);
    }
}



/**
 * chsrc set uv
 */
void
pl_uv_pypi_index_setsrc (char *option)
{
  char *uv_config = pl_py_find_uv_config (true);
  if (!uv_config)
    {
      chsrc_error2 ("无法获取 uv 配置文件路径");
      return;
    }

  char *content = xy_read_file (xy_normalize_path (uv_config));
  if (!content)
    {
      chsrc_error2 ("无法读取 uv 配置文件");
      return false;
    }

  chsrc_use_this_source (pl_uv_pypi_index);

  bool pyproject = xy_str_end_with (uv_config, PL_uv_PyprojectConfigFile);
  const char *index_header = pyproject ? "[[tool.uv.index]]" : "[[index]]";
  const char *parent_table = pyproject ? "[tool.uv]" : NULL;

  char *updated = uvh_replace_index_url (content, source.url, index_header, parent_table);

  chsrc_backup (uv_config);
  chsrc_overwrite_file (updated, uv_config);

  chsrc_determine_chgtype (ChgType_Auto);
  chsrc_conclude (&source);
}


void
pl_uv_pypi_index_resetsrc (char *option)
{
  pl_uv_pypi_index_setsrc (option);
}
