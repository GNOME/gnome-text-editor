/*
 * editor-properties-panel.h
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

#include <adwaita.h>

#include "editor-document.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_PROPERTIES_PANEL (editor_properties_panel_get_type())

G_DECLARE_FINAL_TYPE (EditorPropertiesPanel, editor_properties_panel, EDITOR, PROPERTIES_PANEL, GtkWidget)

GtkWidget      *editor_properties_panel_new          (void);
EditorPage     *editor_properties_panel_get_page     (EditorPropertiesPanel *self);
void            editor_properties_panel_set_page     (EditorPropertiesPanel *self,
                                                      EditorPage            *page);
EditorDocument *editor_properties_panel_get_document (EditorPropertiesPanel *self);
void            editor_properties_panel_set_document (EditorPropertiesPanel *self,
                                                      EditorDocument        *document);

G_END_DECLS
