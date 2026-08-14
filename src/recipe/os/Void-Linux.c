/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_dish(os_voidlinux, "voidlinux");

void
os_voidlinux_prepare ()
{
  chef_prep_this (os_voidlinux, gsr);

  chef_set_recipe_created_on   (this, "2023-09-24");
  chef_set_recipe_last_updated (this, "2025-08-10");

  chef_set_chefs (this, 1, "@ccmywish");
  chef_set_sauciers (this, 1, "@Yangmoooo");

  chef_set_os_scope (this);

  chef_deny_english(this);


  chef_set_note(this, NULL, NULL);

  def_sources_begin()
  {&UpstreamProvider, "https://repo-default.voidlinux.org", FeedByPrepare},
  {&Tuna,             "https://mirrors.tuna.tsinghua.edu.cn/voidlinux", FeedByPrepare},
  {&Sjtug_Zhiyuan,    "https://mirror.sjtu.edu.cn/voidlinux", FeedByPrepare},
  {&Bfsu,             "https://mirrors.bfsu.edu.cn/voidlinux", FeedByPrepare}
  def_sources_end()

  chef_set_rest_smURL_with_postfix (this, "/live/20240314/void-live-x86_64-musl-20240314-xfce.iso");
}


void
os_voidlinux_getsrc (char *option)
{
  char* cmd = "xbps-query -L";
  chsrc_run (cmd, RunOpt_No_Last_New_Line);
}

/**
 * @consult https://help.mirrors.cernet.edu.cn/voidlinux/
 */
void
os_voidlinux_setsrc (char *option)
{
  chsrc_ensure_root ();

  chsrc_use_this_source (os_voidlinux);

  chsrc_ensure_dir ("/etc/xbps.d");
  char *cmd = "cp /usr/share/xbps.d/*-repository-*.conf /etc/xbps.d/";
  chsrc_run (cmd, RunOpt_Default);

  cmd = xy_strcat (3,
            "sed -i 's|https://repo-default.voidlinux.org|", source.url, "|g' /etc/xbps.d/*-repository-*.conf"
            );
  chsrc_run (cmd, RunOpt_Default);

  cmd = xy_strcat (3,
            "sed -i 's|https://alpha.de.repo.voidlinux.org|", source.url, "|g' /etc/xbps.d/*-repository-*.conf"
            );

  chsrc_note2 ("若报错可尝试使用以下命令:");
  p (cmd);

  chsrc_determine_chgtype (ChgType_Untested);
  chsrc_conclude (&source);
}


void
os_voidlinux_resetsrc (char *option)
{
  os_voidlinux_setsrc (option);
}
