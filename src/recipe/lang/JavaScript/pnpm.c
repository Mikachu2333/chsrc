/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_dish(pl_pnpm, "pnpm");

void
pl_pnpm_prepare (void)
{
  chef_prep_this (pl_pnpm, gsr);

  chef_set_recipe_created_on   (this, "2024-04-18");
  chef_set_recipe_last_updated (this, "2025-07-11");

  chef_set_chefs (this, 1, "@ccmywish");
  chef_set_sauciers (this, 0);

  chef_set_scope_cap (this, ProjectScope, ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, UserScope,    ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, SystemScope,  ScopeCap_Unable);
  chef_set_default_scope (this, UserScope);

  chef_allow_english(this);
  chef_allow_user_define(this);

  chef_use_other_target_sources (this, &pl_npm_target);
}


void
pl_pnpm_getsrc (char *option)
{
  chsrc_run ("pnpm config get registry", RunOpt_No_Last_New_Line);
}


/**
 * @consult https://pnpm.io/feature-comparison
 * @consult https://pnpm.io/cli/config
 */
void
pl_pnpm_setsrc (char *option)
{
  Source_t source = chsrc_yield_source (&pl_npm_target, option);
  if (chsrc_in_standalone_mode())
    chsrc_confirm_source(&source);

  char *cmd = NULL;

  if (chsrc_in_project_scope_mode())
    cmd = xy_2strcat ("pnpm config --location project set registry ", source.url);
  else
    cmd = xy_2strcat ("pnpm config -g set registry ", source.url);

  chsrc_run (cmd, RunOpt_No_Last_New_Line);

  if (chsrc_in_standalone_mode())
    {
      chsrc_determine_chgtype (ChgType_Auto);
      chsrc_conclude (&source);
    }
}


void
pl_pnpm_resetsrc (char *option)
{
  pl_pnpm_setsrc (option);
}
