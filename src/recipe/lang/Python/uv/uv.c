/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_target(pl_uv, "uv");

void
pl_uv_prelude (void)
{
  chef_prep_this (pl_uv, gsr);

  chef_set_recipe_created_on   (this, "2026-08-12");
  chef_set_recipe_last_updated (this, "2026-08-12");

  chef_set_sub_targets (this, 2, &pl_uv_pypi_index_target, &pl_uv_python_build_target);

  chef_use_other_target_sources (this, &pl_py_pypi_target);
}


void
pl_uv_getsrc (char *option)
{
  pl_uv_pypi_index_getsrc (option);
  br();
  pl_uv_python_build_getsrc (option);
}



void
pl_uv_setsrc (char *option)
{
  pl_uv_pypi_index_setsrc (option);
  br();
  pl_uv_python_build_setsrc (option);
}



void
pl_uv_resetsrc (char *option)
{
  pl_uv_pypi_index_resetsrc (option);
  br();
  pl_uv_python_build_resetsrc (option);
}
