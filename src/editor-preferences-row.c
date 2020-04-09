/* editor-preferences-row.c
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

#define G_LOG_DOMAIN "editor-preferences-row"

#include "config.h"

#include "editor-preferences-row.h"

G_DEFINE_TYPE (EditorPreferencesRow, editor_preferences_row, GTK_TYPE_LIST_BOX_ROW)

enum {
  ACTIVATED,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void
editor_preferences_row_class_init (EditorPreferencesRowClass *klass)
{
  signals [ACTIVATED] =
    g_signal_new ("activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (EditorPreferencesRowClass, activated),
                  NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
editor_preferences_row_init (EditorPreferencesRow *self)
{
  gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (self), FALSE);
}

void
editor_preferences_row_emit_activated (EditorPreferencesRow *self)
{
  g_return_if_fail (EDITOR_IS_PREFERENCES_ROW (self));

  g_signal_emit (self, signals [ACTIVATED], 0);
}
