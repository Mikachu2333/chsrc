/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * -------------------------------------------------------------
 * 由于Rye已经有后继uv了，所以我们不把该管理器纳入Python group中
 * ------------------------------------------------------------*/

def_target(pl_py_rye, "rye");

void
pl_py_rye_prelude (void)
{
  chef_prep_this (pl_py_rye, gsr);

  chef_set_recipe_created_on   (this, "2024-12-06");
  chef_set_recipe_last_updated (this, "2025-08-09");

  chef_set_chefs (this, 1, "@ccmywish");
  chef_set_sauciers (this, 0);

  chef_set_scope_cap (this, ProjectScope, ScopeCap_Unknown);
  chef_set_scope_cap (this, UserScope,    ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, SystemScope,  ScopeCap_Unable);
  chef_set_default_scope (this, UserScope);

  chef_allow_english(this);
  chef_allow_user_define(this);

  chef_use_other_target_sources (this, &pl_py_pypi_target);
}

char *
pl_python_find_rye_config ()
{
  char *buf = xy_run ("rye config --show-path", 0);
  char *rye_config = xy_normalize_path (buf);

  return rye_config;
}



void
pl_py_rye_getsrc (char *option)
{
  char *rye_config = pl_python_find_rye_config ();
  chsrc_note2 (xy_strcat (3, "请查看 ", rye_config, " 配置文件中的 [[sources]] 节内容"));
}


/**
 * @consult https://github.com/RubyMetric/chsrc/issues/127
 */
void
pl_py_rye_setsrc (char *option)
{
  /* 并不在 Python group 中，所以不考虑 target group 情况，仅使用 Python group 提供的源 */
  Source_t source = chsrc_yield_source_and_confirm (&pl_py_pypi_target, option);

  const char *content = RAWSTR_pl_py_rye_config;

  content = xy_str_gsub (content, "@1@", source.mirror->abbr);
  content = xy_str_gsub (content, "@2@", source.url);

  char *rye_config = pl_python_find_rye_config ();
  chsrc_note2 (xy_strcat (3, "请在配置文件 ", rye_config, " 中添加:"));
  println (content);

  chsrc_determine_chgtype (ChgType_Manual);
  chsrc_conclude (&source);
}


void
pl_py_rye_resetsrc (char *option)
{
  pl_py_rye_setsrc (option);
}
