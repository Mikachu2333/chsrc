/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * -------------------------------------------------------------
 * File Name     : dish.c
 * File Authors  : @ccmywish
 * Contributors  : Nil Null <nil@null.org>
 * Created On    : <2026-08-13>
 * Last Modified : <2026-08-14>
 *
 * dish 为主体的一些函数
 * ------------------------------------------------------------*/

/**
 * @brief 获取 dish 的第一个别名
 *
 * 由 chefs_handle_XXX() 里的 group dish 递归时调用
 */
char *
dish_get_first_alias (Dish_t *target)
{
  if (!target || !target->aliases || *target->aliases == '\0')
    return NULL;

  char *alias = xy_strdup (target->aliases);
  char *separator = strchr (alias, '/');
  if (separator)
    *separator = '\0';
  return alias;
}



bool
dish_has_sub_dishes (Dish_t *target)
{
  return (target->sub_dishes && xy_seq_len(target->sub_dishes) > 1);
}
