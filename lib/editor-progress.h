/*
 * editor-progress.h
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include <glib-object.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_PROGRESS (editor_progress_get_type())

G_DECLARE_FINAL_TYPE (EditorProgress, editor_progress, EDITOR, PROGRESS, GObject)

EditorProgress *editor_progress_new           (void);
char           *editor_progress_dup_message   (EditorProgress *self);
void            editor_progress_set_message   (EditorProgress *self,
                                               const char     *message);
double          editor_progress_get_fraction  (EditorProgress *self);
void            editor_progress_set_fraction  (EditorProgress *self,
                                               double          fraction);
void            editor_progress_file_callback (goffset         current,
                                               goffset         total,
                                               gpointer        user_data);

G_END_DECLS
