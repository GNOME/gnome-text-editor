/* editor-buffer-monitor.h
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

#include <gio/gio.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_BUFFER_MONITOR (editor_buffer_monitor_get_type())

G_DECLARE_FINAL_TYPE (EditorBufferMonitor, editor_buffer_monitor, EDITOR, BUFFER_MONITOR, GObject)

EditorBufferMonitor *editor_buffer_monitor_new         (void);
const char          *editor_buffer_monitor_get_etag    (EditorBufferMonitor *self);
void                 editor_buffer_monitor_set_etag    (EditorBufferMonitor *self,
                                                        const char          *etag);
gboolean             editor_buffer_monitor_get_changed (EditorBufferMonitor *self);
GFile               *editor_buffer_monitor_get_file    (EditorBufferMonitor *self);
void                 editor_buffer_monitor_set_file    (EditorBufferMonitor *self,
                                                        GFile               *file);
void                 editor_buffer_monitor_reset       (EditorBufferMonitor *self);
void                 editor_buffer_monitor_pause       (EditorBufferMonitor *self);
void                 editor_buffer_monitor_unpause     (EditorBufferMonitor *self);
void                 editor_buffer_monitor_set_failed  (EditorBufferMonitor *self,
                                                        gboolean             failed);

G_END_DECLS
