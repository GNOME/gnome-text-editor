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

G_DEFINE_TYPE (EditorPreferencesRow, editor_preferences_row, ADW_TYPE_ACTION_ROW)

static void
editor_preferences_row_class_init (EditorPreferencesRowClass *klass)
{
}

static void
editor_preferences_row_init (EditorPreferencesRow *self)
{
}
