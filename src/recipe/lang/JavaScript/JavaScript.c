/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_combo_dish(pl_js_group, "js/javascript/node/nodejs");

void
pl_js_group_prepare (void)
{
  chef_prep_this_combo_dish (pl_js_group);

  chef_set_recipe_created_on   (this, "2023-09-09");
  chef_set_recipe_last_updated (this, "2026-08-19");

  chef_set_sub_dishes (this, 3, &pl_npm_dish, &pl_yarn_dish, &pl_pnpm_dish);
  chef_set_all_sub_dishes_use_same_source (this, true);
}
