/* editor-spell-menu.c
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <glib/gi18n.h>

#include "editor-spell-menu.h"

GMenuModel *
editor_spell_menu_new (void)
{
  g_autoptr(GMenu) menu = g_menu_new ();
  g_autoptr(GMenu) corrections_menu = g_menu_new ();
  g_autoptr(GMenu) corrections_section = g_menu_new ();
  g_autoptr(GMenu) languages_menu = g_menu_new ();
  g_autoptr(GMenuItem) languages_item = g_menu_item_new_submenu (_("Languages"), G_MENU_MODEL (languages_menu));
  g_autoptr(GMenuItem) add_item = g_menu_item_new (_("Add to Dictionary"), "spellcheck.add");
  g_autoptr(GMenuItem) ignore_item = g_menu_item_new (_("Ignore"), "spellcheck.ignore");
  g_autoptr(GMenuItem) check_item = g_menu_item_new (_("Check Spelling"), "spellcheck.enabled");

  g_menu_item_set_attribute (add_item, "hidden-when", "s", "action-disabled");
  g_menu_item_set_attribute (ignore_item, "hidden-when", "s", "action-disabled");
  g_menu_item_set_attribute (check_item, "role", "s", "check");
  g_menu_item_set_attribute (languages_item, "submenu-action", "s", "spellcheck.enabled");

  g_menu_append_section (corrections_section, NULL, G_MENU_MODEL (corrections_menu));
  g_menu_append_item (corrections_section, add_item);
  g_menu_append_item (corrections_section, ignore_item);
  g_menu_append_section (menu, NULL, G_MENU_MODEL (corrections_section));
  g_menu_append_item (menu, languages_item);
  g_menu_append_item (menu, check_item);

  g_object_set_data_full (G_OBJECT (menu),
                          "LANGUAGES_MENU",
                          g_object_ref (languages_menu),
                          g_object_unref);

  g_object_set_data_full (G_OBJECT (menu),
                          "CORRECTIONS_MENU",
                          g_object_ref (corrections_menu),
                          g_object_unref);

  return G_MENU_MODEL (g_steal_pointer (&menu));
}
