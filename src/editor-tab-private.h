/* editor-tab-private.h
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#pragma once

#include <gtk/gtk.h>

#include "editor-tab.h"
#include "editor-page-private.h"

G_BEGIN_DECLS

struct _EditorTab
{
  GtkWidget    parent_instance;

  EditorPage  *page;
  GtkPopover  *menu_popover;

  GtkBox      *box;
  GtkStack    *stack;
  GtkLabel    *empty;
  GtkLabel    *is_modified;
  GtkLabel    *title;
  GtkLabel    *close_button;
  GtkSpinner  *spinner;
};

void       _editor_tab_class_actions_init (EditorTabClass *klass);
EditorTab *_editor_tab_new                (EditorPage     *page);
void       _editor_tab_actions_init       (EditorTab      *self);
void       _editor_tab_actions_update     (EditorTab      *self);

G_END_DECLS
