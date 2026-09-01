/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_combo_dish(pl_py_group, "py/python");

void
pl_py_group_prepare (void)
{
  chef_prep_this_combo_dish (pl_py_group);

  chef_set_recipe_created_on   (this, "2023-09-03");
  chef_set_recipe_last_updated (this, "2026-09-01");

  chef_set_sub_dishes (this, 3, &pl_pip_dish, &pl_pdm_dish, &pl_poetry_dish);

  chef_all_sub_dishes_use_same_source_from (this, &pl_pypi_dish);
}
