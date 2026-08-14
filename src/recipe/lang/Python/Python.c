/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_group_target(pl_py_group, "py/python");

void
pl_py_group_prepare (void)
{
  chef_prep_this (pl_py_group, NOOP);

  chef_set_recipe_created_on   (this, "2023-09-03");
  chef_set_recipe_last_updated (this, "2026-08-13");

  chef_set_sub_dishes (this, 4, &pl_pip_target, &pl_pdm_target, &pl_poetry_target, &pl_uv_target);
}
