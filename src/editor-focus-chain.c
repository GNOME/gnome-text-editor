/* editor-focus-chain.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include "editor-focus-chain.h"

static gboolean
contains_focus (GtkWidget *widget)
{
  GtkRoot *root = gtk_widget_get_root (widget);
  GtkWidget *focus = gtk_root_get_focus (root);

  if (focus == NULL)
    return FALSE;

  return focus == widget || gtk_widget_is_ancestor (focus, widget);
}

static gboolean
descendant_is_visible (GtkWidget *widget,
                       GtkWidget *descendant)
{
  for (GtkWidget *iter = descendant;
       iter != NULL;
       iter = gtk_widget_get_parent (iter))
    {
      if (!gtk_widget_get_visible (iter) ||
          !gtk_widget_get_child_visible (iter) ||
          !gtk_widget_get_sensitive (iter))
        return FALSE;

      if (iter == widget)
        break;
    }

  return TRUE;
}

#define IS_FORWARD(d) \
  ((d) == GTK_DIR_TAB_FORWARD || (d) == GTK_DIR_RIGHT)
#define BEGIN(q,dir) \
  (IS_FORWARD(dir) ? (q)->head : (q)->tail)
#define MOVE(i,dir) \
  (IS_FORWARD(dir) ? (i)->next : (i)->prev)

gboolean
_editor_focus_chain_focus_child (GtkWidget        *widget,
                                 GQueue           *chain,
                                 GtkDirectionType  dir)
{
  const GList *focus = BEGIN (chain, dir);

  if (!(dir == GTK_DIR_TAB_FORWARD ||
        dir == GTK_DIR_RIGHT ||
        dir == GTK_DIR_TAB_BACKWARD ||
        dir == GTK_DIR_RIGHT))
    return FALSE;

  if (contains_focus (widget))
    {
      for (const GList *iter = BEGIN (chain, dir);
           iter != NULL;
           iter = MOVE (iter, dir))
        {
          if (contains_focus (iter->data))
            {
              focus = MOVE (iter, dir);
              break;
            }
        }
    }

  for (; focus != NULL; focus = MOVE (focus, dir))
    {
      GtkWidget *child = focus->data;

      if (descendant_is_visible (widget, child) &&
          gtk_widget_grab_focus (child))
        return TRUE;
    }

  return FALSE;
}
