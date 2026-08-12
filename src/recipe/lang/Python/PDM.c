/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_target(pl_py_pdm, "pdm");

void
pl_py_pdm_prelude (void)
{
  chef_prep_this (pl_py_pdm, gsr);

  chef_set_recipe_created_on   (this, "2024-06-05");
  chef_set_recipe_last_updated (this, "2025-07-11");

  chef_set_chefs (this, 1, "@ccmywish");
  chef_set_sauciers (this, 0);

  chef_set_scope_cap (this, ProjectScope, ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, UserScope,    ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, SystemScope,  ScopeCap_Unable);
  chef_set_default_scope (this, UserScope);

  chef_allow_english(this);
  chef_allow_user_define(this);

  chef_use_other_target_sources (this, &pl_py_pypi_target);
}


void
pl_py_pdm_getsrc (char *option)
{
  // @HELP 不加 --global 时，会检测到本项目的配置吗？
  // 如果不行的话，就要分别改成 --global 和 --local
  char *cmd = "pdm config pypi.url";
  chsrc_run (cmd, RunOpt_Default);
}


/**
 * @consult https://github.com/RubyMetric/chsrc/issues/19
 */
void
pl_py_pdm_setsrc (char *option)
{
  Source_t source = chsrc_yield_source_and_confirm (&pl_py_pypi_target, option);

  char *cmd = NULL;

  if (chsrc_in_project_scope_mode())
    cmd = xy_2strcat ("pdm config --local pypi.url ", source.url);
  else
    cmd = xy_2strcat ("pdm config --global pypi.url ", source.url);

  chsrc_run (cmd, RunOpt_No_Last_New_Line);

  chsrc_determine_chgtype (ChgType_Auto);
  chsrc_conclude (&source);
}

void
pl_py_pdm_resetsrc (char *option)
{
  pl_py_pdm_setsrc (option);
}
