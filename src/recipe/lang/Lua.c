/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

static MirrorSite_t Api7 =
{
  IS_DedicatedMirrorSite,
  "api7", "api7.ai", "深圳支流科技有限公司", "https://www.apiseven.com/",
  {SKIP, ToFill, ToFill, NULL, ROUGH}
};

def_dish(pl_lua, "lua/luarocks");

void
pl_lua_prepare ()
{
  chef_prep_this (pl_lua, gs);

  chef_set_recipe_created_on   (this, "2023-09-27");
  chef_set_recipe_last_updated (this, "2025-08-22");

  chef_set_chefs (this, 1, "@ccmywish");
  chef_set_sauciers (this, 1, "@hezonglun");

  chef_set_scope_cap (this, ProjectScope, ScopeCap_Unknown);
  chef_set_scope_cap (this, UserScope,    ScopeCap_Able_And_Implemented);
  chef_set_scope_cap (this, SystemScope,  ScopeCap_Unknown);
  chef_set_default_scope (this, UserScope);

  chef_deny_english(this);
  chef_allow_user_define(this);

  def_sources_begin()
  {&UpstreamProvider, "https://luarocks.org", DelegateToUpstream},
  {&Api7,             "https://luarocks.cn", DelegateToMirror}
  def_sources_end()
}


void
pl_lua_getsrc (char *option)
{
  chsrc_view_file ("~/.luarocks/config.lua");
  chsrc_view_file ("~/.luarocks/upload_config.lua");
}

/**
 * @consult https://luarocks.cn/
 */
void
pl_lua_setsrc (char *option)
{
  chsrc_use_this_source (pl_lua);

  char *config = xy_strcat (3, "rocks_servers = {\n"
                                "  \"", source.url, "\"\n"
                                "}");

  chsrc_note2 ("请手动修改 ~/.luarocks/config.lua 文件 (用于下载):");
  println (config);

  char *upload_config = xy_strcat (3, "key = \"<Your API Key>\"\n"
                                      "server = \"", source.url, "\"");

  chsrc_note2 ("请手动修改 ~/.luarocks/upload_config.lua 文件 (用于上传):");
  println (upload_config);

  chsrc_conclude (&source);
}
