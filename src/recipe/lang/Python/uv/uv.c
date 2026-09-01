/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_combo_dish(pl_uv, "uv");

void
pl_uv_prepare (void)
{
  chef_prep_this_combo_dish (pl_uv);

  chef_set_recipe_created_on   (this, "2026-08-12");
  chef_set_recipe_last_updated (this, "2026-08-20");

  chef_set_sub_dishes (this, 2, &pl_uv_pypi_index_dish,
                                &pl_uv_python_build_dish);
}
