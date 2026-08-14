/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_group_target(pl_uv, "uv");

void
pl_uv_prepare (void)
{
  chef_prep_this (pl_uv, NOOP);

  chef_set_recipe_created_on   (this, "2026-08-12");
  chef_set_recipe_last_updated (this, "2026-08-13");

  chef_set_sub_dishes (this, 2, &pl_uv_pypi_index_target, &pl_uv_python_build_target);

  chef_use_other_target_sources (this, &pl_pypi_target);
}
