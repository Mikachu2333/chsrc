/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_target(pl_py_pip, "pip");

void
pl_py_pip_prelude (void)
{
  chef_prep_this (pl_py_pip, gsr);

  chef_set_recipe_created_on   (this, "2023-09-03");
  chef_set_recipe_last_updated (this, "2025-09-12");

  chef_set_chefs (this, 1, "@ccmywish");
  chef_set_sauciers (this, 1, "@happy-game");

  chef_set_scope_cap (this, ProjectScope, ScopeCap_Unable);
  chef_set_scope_cap (this, UserScope,    ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, SystemScope,  ScopeCap_Unable);
  chef_set_default_scope (this, UserScope);

  chef_allow_english(this);
  chef_allow_user_define(this);

  chef_use_other_target_sources (this, &pl_py_group_target);
}


void
pl_py_pip_getsrc (char *option)
{
  char *py_prog_name = NULL;
  pl_py_get_python_program_name (&py_prog_name);

  char *cmd = xy_2strcat (py_prog_name, " -m pip config get global.index-url");

  int status = chsrc_run_directly (cmd);
  if (0 == status)
    {
      // 执行成功时显示当前源
      xy_noop ();
    }
  else
    {
      // 执行失败时显示默认源
      if (ENGLISH)
        chsrc_note2 ("No source configured in pip, showing default upstream source:");
      else
        chsrc_note2 ("pip 中未配置源，显示默认上游源：");

      Source_t default_source = chsrc_yield_source (&pl_py_group_target, "upstream");
      say (default_source.url);
    }
}


/**
 * @consult https://mirrors.tuna.tsinghua.edu.cn/help/pypi/
 */
void
pl_py_pip_setsrc (char *option)
{
  // 对于不支持的情况，尽早结束
  if (chsrc_in_project_scope_mode())
    {
      char *msg = ENGLISH ? "pip doesn't support `-scope=project`. SKIP changing source!"
                          : "pip 不支持项目级换源，跳过换源";
      chsrc_error (msg);
      // 不能直接退出，因为 Leader target 不能就此结束
      return;
    }

  Source_t source = chsrc_yield_source (&pl_py_group_target, option);
  if (chsrc_in_standalone_mode())
    chsrc_confirm_source(&source);

  char *py_prog_name = NULL;
  pl_py_get_python_program_name (&py_prog_name);

  // 这里用的是 config --user，会写入用户目录（而不是项目目录）
  // https://github.com/RubyMetric/chsrc/issues/39
  // 经测试，Windows上调用换源命令，会写入 C:\Users\RubyMetric\AppData\Roaming\pip\pip.ini
  char *cmd = xy_2strcat (py_prog_name, xy_2strcat (" -m pip config --user set global.index-url ", source.url));
  chsrc_run (cmd, RunOpt_No_Last_New_Line);

  if (chsrc_in_standalone_mode())
    {
      chsrc_determine_chgtype (ChgType_Auto);
      chsrc_conclude (&source);
    }
}


void
pl_py_pip_resetsrc (char *option)
{
  pl_py_pip_setsrc (option);
}
