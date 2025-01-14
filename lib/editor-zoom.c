/*
 * editor-zoom.c
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

#include "config.h"

#include "editor-zoom.h"

G_DEFINE_ENUM_TYPE (EditorZoom, editor_zoom,
                    G_DEFINE_ENUM_VALUE (EDITOR_ZOOM_MINUS_FIVE, "minus-five"),
                    G_DEFINE_ENUM_VALUE (EDITOR_ZOOM_MINUS_FOUR, "minus-four"),
                    G_DEFINE_ENUM_VALUE (EDITOR_ZOOM_MINUS_THREE, "minus-three"),
                    G_DEFINE_ENUM_VALUE (EDITOR_ZOOM_MINUS_TWO, "minus-two"),
                    G_DEFINE_ENUM_VALUE (EDITOR_ZOOM_MINUS_ONE, "minus-one"),
                    G_DEFINE_ENUM_VALUE (EDITOR_ZOOM_DEFAULT, "default"),
                    G_DEFINE_ENUM_VALUE (EDITOR_ZOOM_PLUS_ONE, "plus-one"),
                    G_DEFINE_ENUM_VALUE (EDITOR_ZOOM_PLUS_TWO, "plus-two"),
                    G_DEFINE_ENUM_VALUE (EDITOR_ZOOM_PLUS_THREE, "plus-three"),
                    G_DEFINE_ENUM_VALUE (EDITOR_ZOOM_PLUS_FOUR, "plus-four"),
                    G_DEFINE_ENUM_VALUE (EDITOR_ZOOM_PLUS_FIVE, "plus-five"))
