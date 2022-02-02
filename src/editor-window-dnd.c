/* editor-window-dnd.c
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

#define G_LOG_DOMAIN "editor-window-dnd"

#include "config.h"

#include "editor-session.h"
#include "editor-window-private.h"

static gboolean
editor_window_drop_target_drop (EditorWindow  *self,
                                const GValue  *value,
                                gdouble        x,
                                gdouble        y,
                                GtkDropTarget *dest)
{
  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (GTK_IS_DROP_TARGET (dest));

  if (G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
    {
      EditorSession *session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);
      GSList *list = g_value_get_boxed (value);

      for (const GSList *iter = list; iter; iter = iter->next)
        {
          GFile *file = iter->data;
          g_assert (G_IS_FILE (file));
          editor_session_open (session, self, file, NULL);
        }

      return TRUE;
    }

  return FALSE;
}

void
_editor_window_dnd_init (EditorWindow *self)
{
  GtkDropTarget *dest;

  dest = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
  g_signal_connect_object (dest,
                           "drop",
                           G_CALLBACK (editor_window_drop_target_drop),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (dest));
}
