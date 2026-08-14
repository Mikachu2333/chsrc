/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_dish(pl_clojure, "clojure/clojars/cloj/lein");

void
pl_clojure_prepare ()
{
  chef_prep_this_dish (pl_clojure, s);

  chef_set_recipe_created_on   (this, "2023-09-10");
  chef_set_recipe_last_updated (this, "2026-02-24");

  chef_set_chefs (this, 1, "@ccmywish");
  chef_set_sauciers (this, 1, "@hezonglun");

  chef_set_scope_cap (this, ProjectScope, ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, UserScope,    ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, SystemScope,  ScopeCap_Unable);
  chef_set_default_scope (this, UserScope);

  chef_deny_english(this);
  chef_allow_user_define(this);

  def_sources_begin()
  {&UpstreamProvider,"https://repo.clojars.org/", DelegateToUpstream},
  {&MirrorZ,         "https://mirrors.cernet.edu.cn/clojars/",        DelegateToMirror},
  {&Tuna,            "https://mirrors.tuna.tsinghua.edu.cn/clojars/", DelegateToMirror},
  {&Nju,             "https://mirror.nju.edu.cn/clojars/",             DelegateToMirror},
  {&Nyist,           "https://mirror.nyist.edu.cn/clojars/",          DelegateToMirror},
  {&Ustc,            "https://mirrors.ustc.edu.cn/clojars/",          DelegateToMirror},
  {&Iscas,           "https://mirror.iscas.ac.cn/clojars/",           DelegateToMirror}
  def_sources_end()
}


void
pl_clojure_setsrc (char *option)
{
  chsrc_use_this_source (pl_clojure);

  if (chsrc_in_project_scope_mode())
    {
      chsrc_note2 ("请在项目根目录中的 project.clj 中手动添加 :mirrors 关键字:");
      char *config = xy_str_gsub (RAWSTR_pl_clojure_project_clj, "@url@", source.url);
      println (config);
    }
  else
    {
      chsrc_note2 ("请在 ~/.lein/projfiles.clj 中手动添加 :mirrors 关键字:");
      char *config = xy_str_gsub (RAWSTR_pl_clojure_projfiles_clj, "@url@", source.url);
      println (config);
    }

  chsrc_determine_chgtype (ChgType_Manual);
  chsrc_conclude (&source);
}
