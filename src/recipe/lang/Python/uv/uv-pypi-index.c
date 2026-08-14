/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_dish(pl_uv_pypi_index, "uv-pypi-index");

void
pl_uv_pypi_index_prepare (void)
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

  chef_use_other_target_sources (this, &pl_pypi_target);
}



void
pl_uv_pypi_index_getsrc (char *option)
{
  char *uv_config = pl_uv_find_uv_config (false);

  if (!uv_config || !chsrc_check_file (uv_config))
    {
      if (!uv_config)
        chsrc_error2 ("无法获取 uv 配置文件路径");
      else
        chsrc_error2 ("未找到 uv 配置文件");
      return;
    }

  // uv.toml 与 pyproject.toml 均使用同一套受限 TOML 读取逻辑。
  char *content = xy_file_read (xy_normalize_path (uv_config));
  if (!content)
    {
      chsrc_error2 ("无法读取 uv 配置文件");
      return;
    }

  bool pyproject = xy_str_end_with (uv_config, PL_uv_PyprojectConfigFile);
  const char *index_header = pyproject ? "[[tool.uv.index]]" : "[[index]]";

  char *url = pl_uv_toml_get_index_url (content, index_header);
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
      Source_t default_source = chsrc_yield_source (&pl_pypi_target, "upstream");
      println (default_source.url);
    }
}



/**
 * chsrc set uv
 */
void
pl_uv_pypi_index_setsrc (char *option)
{
  char *uv_config = pl_uv_find_uv_config (true);
  if (!uv_config)
    {
      chsrc_error2 ("无法获取 uv 配置文件路径");
      return;
    }

  char *content = xy_file_read (xy_normalize_path (uv_config));
  if (!content)
    {
      chsrc_error2 ("无法读取 uv 配置文件");
      return;
    }

  chsrc_use_this_source (pl_uv_pypi_index);

  bool pyproject = xy_str_end_with (uv_config, PL_uv_PyprojectConfigFile);
  const char *index_header = pyproject ? "[[tool.uv.index]]" : "[[index]]";
  const char *parent_table = pyproject ? "[tool.uv]" : NULL;

  char *updated = pl_uv_toml_replace_index_url (content, source.url, index_header, parent_table);

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
