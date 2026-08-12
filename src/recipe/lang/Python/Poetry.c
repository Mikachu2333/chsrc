/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_target(pl_py_poetry, "poetry");

void
pl_py_poetry_prelude (void)
{
  chef_prep_this (pl_py_poetry, gsr);

  chef_set_recipe_created_on   (this, "2024-08-08");
  chef_set_recipe_last_updated (this, "2025-07-11");

  chef_set_chefs (this, 1, "@ccmywish");
  chef_set_sauciers (this, 0);

  /* Poetry 仅支持项目级换源 */
  chef_set_scope_cap (this, ProjectScope, ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, UserScope,    ScopeCap_Unable);
  chef_set_scope_cap (this, SystemScope,  ScopeCap_Unable);
  chef_set_default_scope (this, ProjectScope);

  chef_allow_english(this);
  chef_allow_user_define(this);

  chef_use_other_target_sources (this, &pl_py_pypi_target);
}

void
pl_py_poetry_getsrc (char *option)
{
  chsrc_note2 ("poetry换源情况: 请查看本项目 pyproject.toml 中 [[tool.poetry.source]]");
}


/**
 * @consult https://python-poetry.org/docs/repositories/#project-configuration
 */
void
pl_py_poetry_setsrc (char *option)
{
  Source_t source = chsrc_yield_source_and_confirm (&pl_py_pypi_target, option);

  char *cmd = xy_2strcat ("poetry source add my_mirror ", source.url);
  chsrc_run (cmd, RunOpt_No_Last_New_Line);

  chsrc_determine_chgtype (ChgType_Auto);
  chsrc_conclude (&source);
}


void
pl_py_poetry_resetsrc (char *option)
{
  pl_py_poetry_setsrc (option);
}
