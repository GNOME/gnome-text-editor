/* editor-types.h
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

#include "editor-types.h"

G_BEGIN_DECLS

#define ADW_TYPE_ACTION_ROW (editor_preferences_row_get_type())

G_DECLARE_DERIVABLE_TYPE (AdwActionRow, editor_preferences_row, EDITOR, PREFERENCES_ROW, AdwActionRow)

struct _AdwActionRowClass
{
  AdwActionRowClass parent_class;
};

AdwActionRow *editor_preferences_row_new            (void);

G_END_DECLS
