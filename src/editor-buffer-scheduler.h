/* editor-buffer-scheduler.h
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

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef gboolean (*EditorBufferCallback) (gint64   deadline,
                                          gpointer user_data);

gsize editor_buffer_scheduler_add      (EditorBufferCallback  callback,
                                        gpointer              user_data);
gsize editor_buffer_scheduler_add_full (EditorBufferCallback  callback,
                                        gpointer              user_data,
                                        GDestroyNotify        notify);
void  editor_buffer_scheduler_remove   (gsize                 handler_id);

static inline void
editor_buffer_scheduler_clear (gsize *handler_id_ptr)
{
  gsize val = *handler_id_ptr;

  if (val)
    {
      *handler_id_ptr = 0;
      editor_buffer_scheduler_remove (val);
    }
}

G_END_DECLS
