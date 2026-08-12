/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

static MirrorSite_t NpmMirror =
{
  IS_DedicatedMirrorSite,
  "npmmirror", "npmmirror", "npmmirror (阿里云赞助)", "https://npmmirror.com/",
  {SKIP, NULL, NULL, NULL, ACCURATE}
};

def_target(pl_js_group, "js/javascript/node/nodejs");

void
pl_js_group_prelude (void)
{
  chef_prep_this (pl_js_group, gsr);

  chef_set_recipe_created_on   (this, "2023-09-09");
  chef_set_recipe_last_updated (this, "2025-07-11");

  // 组换源的 leader target 应把所有 follower target 的贡献者都记录过来
  chef_set_chefs (this, 1, "@ccmywish");
  chef_set_sauciers (this, 2, "@lontten", "@MrWillCom");

  /* ProjectScope 支持 npm, yarn v2, pnpm, 不支持 yarn v1 */
  chef_set_scope_cap (this, ProjectScope, ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, UserScope,    ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, SystemScope,  ScopeCap_Unknown);
  chef_set_default_scope (this, UserScope);

  chef_allow_english(this);
  chef_allow_user_define(this);

  def_sources_begin()
  {&UpstreamProvider, "https://registry.npmjs.org/",    FeedByPrelude}, /* @note 根据 pnpm 官网，有最后的斜线 */
  {&NpmMirror,        "https://registry.npmmirror.com", FeedByPrelude},
  {&Huawei,           "https://mirrors.huaweicloud.com/repository/npm/", FeedByPrelude},
  {&Tencent,          "https://mirrors.cloud.tencent.com/npm/", FeedByPrelude}
  def_sources_end()

  // 29MB 大小
  chef_set_rest_smURL_with_postfix (this, "/@tensorflow/tfjs/-/tfjs-4.22.0.tgz");
}


void
pl_js_check_cmd (bool *npm_exist, bool *yarn_exist, bool *pnpm_exist)
{
  *npm_exist  = chsrc_check_program ("npm");
  *yarn_exist = chsrc_check_program ("yarn");
  *pnpm_exist = chsrc_check_program ("pnpm");

  if (!*npm_exist && !*yarn_exist && !*pnpm_exist)
    {
      char *msg = ENGLISH ? "No npm, yarn or pnpm command found, check if at least one is present"
                          : "未找到 npm 或 yarn 或 pnpm 命令，请检查是否存在其一";
      chsrc_error (msg);
      exit (Exit_UserCause);
    }
}


void
pl_js_group_getsrc (char *option)
{
  bool npm_exist, yarn_exist, pnpm_exist;
  pl_js_check_cmd (&npm_exist, &yarn_exist, &pnpm_exist);

  hr();

  if (npm_exist)
    {
      pl_npm_getsrc (option);
      br();
    }

  if (yarn_exist)
    {
      pl_yarn_getsrc (option);
      br();
    }

  if (pnpm_exist)
    {
      pl_pnpm_getsrc (option);
      br();
    }
}


void
pl_js_group_setsrc (char *option)
{
  {
    char *msg = ENGLISH ? "Three package managers will be replaced for you at the same time: "
                          "npm, pnpm, yarn. If you need to change the source independently, "
                          "please run independently `chsrc set <pkg-manager>`"
                        : "将同时更换3个包管理器 npm, pnpm, Yarn 的源，若需要独立换源，请独立运行 chsrc set <pkg-manager>";
    chsrc_alert2 (msg);
  }

  bool npm_exist, yarn_exist, pnpm_exist;
  pl_js_check_cmd (&npm_exist, &yarn_exist, &pnpm_exist);

  chsrc_set_target_group_mode ();

  chsrc_use_this_source (pl_js_group);

  if (npm_exist)
    {
      pl_npm_setsrc (option);
      br();
    }

  if (yarn_exist)
    {
      pl_yarn_setsrc (option);
      br();
    }

  if (pnpm_exist)
    {
      pl_pnpm_setsrc (option);
    }

  chsrc_determine_chgtype (ChgType_Auto);
  chsrc_conclude (&source);
}


void
pl_js_group_resetsrc (char *option)
{
  pl_js_group_setsrc (option);
}
