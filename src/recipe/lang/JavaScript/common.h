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



def_sources_target(pl_js_nodejs_binary, "nodejs-binary");

void
pl_js_nodejs_binary_prelude (void)
{
  chef_prep_sources_target (pl_js_nodejs_binary);

  chef_set_recipe_created_on   (this, "2023-09-09");
  chef_set_recipe_last_updated (this, "2026-08-12");

  chef_set_chefs (this, 1, "@ccmywish");
  chef_set_sauciers (this, 0);

  chef_allow_user_define(this);

  def_sources_begin()
  {&UpstreamProvider,  "https://nodejs.org/dist/", FeedByPrelude},
  {&NpmMirror, "https://npmmirror.com/mirrors", FeedByPrelude},
  {&Tuna,      "https://mirrors.tuna.tsinghua.edu.cn/nodejs-release/",FeedByPrelude},
  {&Bfsu,      "https://mirrors.bfsu.edu.cn/nodejs-release/",FeedByPrelude},
  {&Ustc,      "https://mirrors.ustc.edu.cn/node/",FeedByPrelude},
  {&Huawei,    "https://mirrors.huaweicloud.com/nodejs/",FeedByPrelude},
  {&Tencent,   "https://mirrors.cloud.tencent.com/nodejs-release/", FeedByPrelude}
  def_sources_end()

  chef_set_rest_smURL_with_postfix (this, "/v23.4.0/node-v23.4.0-linux-x64.tar.xz");
}
