/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

/*
 * Python下载镜像 (python-install-mirror)
 *
 * 内部源target, 仅用于 `uv` recipe 独立选择 Python 下载镜像。
 * 针对 python-build-standalone 的测速链接。
*/

def_sources_target(pl_py_uv_python_build, "uv-python-build");

// CNB (Cloud Native Build, 腾讯) 托管的 python-build-standalone releases 镜像
static MirrorSite_t CnbUvPython =
{
  IS_DedicatedMirrorSite,
  "cnb", "CNB", "Cloud Native Build (腾讯)", "https://cnb.cool/",
  {NotSkip, NA, NA, "https://cnb.cool/astral-sh/python-build-standalone/-/releases/download/20260728/cpython-3.15.0b4+20260728-x86_64_v4-unknown-linux-gnu-freethreaded-install_only.tar.gz", ACCURATE}
};

void
pl_py_uv_python_build_prelude (void)
{
  chef_prep_sources_target (pl_py_uv_python_build);

  chef_set_recipe_created_on   (this, "2026-08-02");
  chef_set_recipe_last_updated (this, "2026-08-11");

  chef_set_chefs (this, 1, "@Mikachu2333");
  chef_set_sauciers (this, 1, "@ccmywish");

  def_sources_begin ()
  {&UpstreamProvider, "https://github.com/astral-sh/python-build-standalone/releases/download",       DelegateToUpstream},
  {&Nju,              "https://mirrors.nju.edu.cn/github-release/astral-sh/python-build-standalone",  FeedByPrelude},
  {&Ustc,             "https://mirrors.ustc.edu.cn/github-release/astral-sh/python-build-standalone", FeedByPrelude},
  {&Lzuoss,           "https://mirror.lzu.edu.cn/github-release/astral-sh/python-build-standalone",   FeedByPrelude},
  {&CnbUvPython,      "https://cnb.cool/astral-sh/python-build-standalone/-/releases/download",       FeedByPrelude}
  def_sources_end ()

#define GH_SM_POSTFIX  "/20260728/cpython-3.14.6+20260728-i686-pc-windows-msvc-install_only_stripped.tar.gz"
  chef_set_smURL_with_postfix (this, &Nju,         GH_SM_POSTFIX);
  chef_set_smURL_with_postfix (this, &Lzuoss,      GH_SM_POSTFIX);
  chef_set_smURL_with_postfix (this, &CnbUvPython, GH_SM_POSTFIX);
#undef GH_SM_POSTFIX

  // 2026-5-31: USTC 仅保留 Latest, 只能用 SHA256SUMS 粗略测速
  chef_set_smURL_with_postfix (this, &Ustc, "/LatestRelease/SHA256SUMS");

  // 中科大仅保留 Latest 且文件内含动态版本号, 使用模糊测速
  chef_set_provider_sm_accuracy (&Ustc, ROUGH);
}
