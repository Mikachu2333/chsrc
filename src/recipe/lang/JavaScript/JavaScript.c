/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ------------------------------------------------------------*/

def_group_target(pl_js_group, "js/javascript/node/nodejs");

void
pl_js_group_prepare (void)
{
  chef_prep_this (pl_js_group, NOOP);

  chef_set_recipe_created_on   (this, "2023-09-09");
  chef_set_recipe_last_updated (this, "2026-08-13");

  chef_set_sub_dishes (this, 3, &pl_npm_target, &pl_yarn_target, &pl_pnpm_target);
}
