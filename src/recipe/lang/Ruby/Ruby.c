/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

static MirrorSite_t RubyChina =
{
  IS_DedicatedMirrorSite,
  "rubychina", "RubyChina", "Ruby China 社区", "https://gems.ruby-china.com/",
  {NotSkip, NA, NA, "https://gems.ruby-china.com/rubygems/gems/nokogiri-1.15.0-java.gem", ACCURATE} // 9.9 MB
};

def_dish(pl_ruby, "gem/ruby/rb/rubygem/rubygems/bundler");

void
pl_ruby_prepare (void)
{
  chef_prep_this_dish (pl_ruby, gsr);

  chef_set_recipe_created_on   (this, "2023-08-29");
  chef_set_recipe_last_updated (this, "2026-08-14");

  chef_set_chefs (this, 1, "@ccmywish");
  chef_set_sauciers (this, 1, "@BingChunMoLi");

  /* 支持 bundler. 不支持 gem */
  chef_set_scope_cap (this, ProjectScope, ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, UserScope,    ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, SystemScope,  ScopeCap_Unable);
  chef_set_default_scope (this, UserScope);

  chef_allow_english(this);
  chef_allow_user_define(this);

  def_sources_begin()
  {&UpstreamProvider, "https://rubygems.org/",        "https://rubygems.org/gems/nokogiri-1.15.0-java.gem"},

  // https://ruby-china.org/topics/44619
  // {&RubyChina,        "https://gems.ruby-china.com/",                   DelegateToMirror},

  {&Ustc,             "https://mirrors.ustc.edu.cn/rubygems/",          DelegateToMirror}

  // {&Tuna,          "https://mirrors.tuna.tsinghua.edu.cn/rubygems/", DelegateToMirror},
  // {&Bfsu,          "https://mirrors.bfsu.edu.cn/rubygems/",          DelegateToMirror},

  // {&Tencent,       "https://mirrors.tencent.com/rubygems/",                DelegateToMirror},
  // {&Tencent_Intra, "https://mirrors.tencentyun.com/rubygems/",             DelegateToMirror},
  // {&Ali,           "https://mirrors.aliyun.com/rubygems/",                 DelegateToMirror},
  // {&Huawei,        "https://mirrors.huaweicloud.com/repository/rubygems/", DelegateToMirror},
  def_sources_end()
}


void
pl_ruby_getsrc (char *option)
{
  chsrc_run ("gem sources", RunOpt_Default);
  chsrc_run ("bundle config get mirror.https://rubygems.org", RunOpt_Default);
}

bool
pl_ruby_remove_gem_source (const char *source)
{
  char *cmd = NULL;
  if (hp_is_url (source))
    {
      cmd = xy_2strcat ("gem sources -r ", source);
      chsrc_run (cmd, RunOpt_Default);
    }
  return false;
}

/**
 * @consult https://gitee.com/RubyMetric/rbenv-cn
 */
void
pl_ruby_setsrc (char *option)
{
  chsrc_ensure_program ("gem");

  chsrc_use_this_source (pl_ruby);

  char *cmd = NULL;

  // step1
  xy_run_iter_lines ("gem sources -l", 0, pl_ruby_remove_gem_source);

  cmd = xy_2strcat ("gem source -a ", source.url);
  chsrc_run (cmd, RunOpt_Default);

  // 我们在 step1 中，把源全部清空了，但是现在 RubyGems 的行为是: 当清空会自动给你把默认源给加回来
  // 所以我们在这一步，最后一次删除默认源，确保它不存在
  pl_ruby_remove_gem_source (this->sources[0].url);


  chsrc_ensure_program ("bundle");

  char *where = " --global ";
  if (chsrc_in_project_scope_mode())
    {
      where = " --local ";
    }

  cmd = xy_strcat (4, "bundle config set", where, "'mirror.https://rubygems.org' ", source.url);
  chsrc_run (cmd, RunOpt_No_Last_New_Line);

  chsrc_determine_chgtype (ChgType_Auto);
  chsrc_conclude (&source);
}

void
pl_ruby_resetsrc (char *option)
{
  pl_ruby_setsrc (option);
}
