/* editor-signal-group.h
 *
 * Copyright (C) 2015 Christian Hergert <christian@hergert.me>
 * Copyright (C) 2015 Garrett Regier <garrettregier@gmail.com>
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_SIGNAL_GROUP (editor_signal_group_get_type())

G_DECLARE_FINAL_TYPE (EditorSignalGroup, editor_signal_group, EDITOR, SIGNAL_GROUP, GObject)

EditorSignalGroup *editor_signal_group_new             (GType              target_type);
void               editor_signal_group_set_target      (EditorSignalGroup *self,
                                                        gpointer           target);
gpointer           editor_signal_group_get_target      (EditorSignalGroup *self);
void               editor_signal_group_block           (EditorSignalGroup *self);
void               editor_signal_group_unblock         (EditorSignalGroup *self);
void               editor_signal_group_connect_object  (EditorSignalGroup *self,
                                                        const gchar       *detailed_signal,
                                                        GCallback          c_handler,
                                                        gpointer           object,
                                                        GConnectFlags      flags);
void               editor_signal_group_connect_data    (EditorSignalGroup *self,
                                                        const gchar       *detailed_signal,
                                                        GCallback          c_handler,
                                                        gpointer           data,
                                                        GClosureNotify     notify,
                                                        GConnectFlags      flags);
void               editor_signal_group_connect         (EditorSignalGroup *self,
                                                        const gchar       *detailed_signal,
                                                        GCallback          c_handler,
                                                        gpointer           data);
void               editor_signal_group_connect_after   (EditorSignalGroup *self,
                                                        const gchar       *detailed_signal,
                                                        GCallback          c_handler,
                                                        gpointer           data);
void               editor_signal_group_connect_swapped (EditorSignalGroup *self,
                                                        const gchar       *detailed_signal,
                                                        GCallback          c_handler,
                                                        gpointer           data);

G_END_DECLS
