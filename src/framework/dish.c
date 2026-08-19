/** ------------------------------------------------------------
 * SPDX-License-Identifier: GPL-3.0-or-later
 * -------------------------------------------------------------
 * File Name     : dish.c
 * File Authors  : @ccmywish
 * Contributors  : @Mikachu2333
 *               |
 * Created On    : <2026-08-13>
 * Last Modified : <2026-08-19>
 *
 * dish 为主体的一些函数
 * ------------------------------------------------------------*/

/**
 * @brief 获取 dish 的第一个别名
 *
 * 由 chefs_handle_XXX() 里的 group dish 递归时调用
 */
char *
dish_get_first_alias (Dish_t *dish)
{
  if (!dish || !dish->aliases || *dish->aliases == '\0')
    return NULL;

  char *alias = xy_strdup (dish->aliases);
  char *separator = strchr (alias, '/');
  if (separator)
    *separator = '\0';
  return alias;
}



bool
dish_has_sub_dishes (Dish_t *dish)
{
  return (dish->sub_dishes && xy_seq_len(dish->sub_dishes) > 1);
}



/**
 * @brief 判断 dish 是否拥有来自指定镜像站的源
 *
 * @note 支持两个 "伪 mirror code": `upstream` 与 `first`
 */
bool
dish_has_source_from_mirror (Dish_t *dish, const char *mirror_code)
{
  if (!dish || !dish->sources || !code)
    return false;

  if (xy_streql (mirror_code, "upstream"))
    return dish->sources_n > 0;

  if (xy_streql (mirror_code, "first"))
    return dish->sources_n > 1;

  for (int i=0; i < dish->sources_n; i++)
    {
      if (xy_streql (dish->sources[i].mirror->code, mirror_code))
        return true;
    }

  return false;
}


/**
 * @brief 判断 dish 的兄弟 sub dish 是否拥有来自指定镜像站的源
 *
 * @example
 *
 *          / `dish`   current dish as a sub dish
 *   combo -  sibling 1 sub dish
 *          \ sibling 2 sub dish
 */
static bool
subdish_sibling_has_source_from_mirror (Dish_t *dish, const char *mirror_code)
{
  Dish_t *current = dish;

  if (ProgStatus.ComboStackDepth <= 0)
    return false;

  Dish_t *combo = ProgStatus.ComboStack[ProgStatus.ComboStackDepth - 1];
  if (!combo || !combo->sub_dishes)
    return false;

  for (size_t i=0; i < xy_seq_len(combo->sub_dishes); i++)
    {
      Dish_t *sub_dish = xy_seq_at (combo->sub_dishes, i);
      if (sub_dish == current)
        continue;

      if (!sub_dish->inited)
        sub_dish->preparefn ();

      if (dish_has_source_from_mirror (sub_dish, mirror_code))
        return true;
    }

  return false;
}
