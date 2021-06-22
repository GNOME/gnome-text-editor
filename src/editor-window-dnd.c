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

#if 0
static void
editor_window_dnd_drag_data_received_cb (EditorWindow     *self,
                                         GdkDragContext   *context,
                                         gint              x,
                                         gint              y,
                                         GtkSelectionData *data,
                                         guint             info,
                                         guint             time_,
                                         GtkWidget        *widget)
{
  g_auto(GStrv) uris = NULL;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (GDK_IS_DRAG_CONTEXT (context));
  g_assert (GTK_IS_WIDGET (widget));

  if (!(uris = gtk_selection_data_get_uris (data)) || uris[0] == NULL)
    return;

  for (guint i = 0; uris[i] != NULL; i++)
    {
      g_autoptr(GFile) file = g_file_new_for_uri (uris[i]);

      if (file != NULL)
        editor_session_open (EDITOR_SESSION_DEFAULT, self, file, NULL);
    }

  gtk_window_present_with_time (GTK_WINDOW (self), time_);
}
#endif

void
_editor_window_dnd_init (EditorWindow *self)
{
#if 0
  static const GtkTargetEntry target_entries[] = {
    { (gchar *)"text/uri-list", 0, 0 },
  };

  gtk_drag_dest_unset (GTK_WIDGET (self->notebook));

  gtk_drag_dest_set (GTK_WIDGET (self->paned),
                     GTK_DEST_DEFAULT_ALL,
                     target_entries,
                     G_N_ELEMENTS (target_entries),
                     GDK_ACTION_COPY);

  g_signal_connect_object (self->paned,
                           "drag-data-received",
                           G_CALLBACK (editor_window_dnd_drag_data_received_cb),
                           self,
                           G_CONNECT_SWAPPED);

#endif
}
