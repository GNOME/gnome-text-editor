/*
 * modeline-parser.h
 * Emacs, Kate and Vim-style modelines support for gedit.
 *
 * Copyright 2005-2007 Steve Frécinaux <code@istique.net>
 * Copyright 2020      Christian Hergert <chergert@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum
{
  MODELINE_SET_NONE = 0,
  MODELINE_SET_TAB_WIDTH = 1 << 0,
  MODELINE_SET_INDENT_WIDTH = 1 << 1,
  MODELINE_SET_WRAP_MODE = 1 << 2,
  MODELINE_SET_SHOW_RIGHT_MARGIN = 1 << 3,
  MODELINE_SET_RIGHT_MARGIN_POSITION = 1 << 4,
  MODELINE_SET_LANGUAGE = 1 << 5,
  MODELINE_SET_INSERT_SPACES = 1 << 6
} ModelineSet;

typedef struct _ModelineOptions
{
  gchar       *language_id;

  gboolean     insert_spaces;
  guint        tab_width;
  guint        indent_width;
  GtkWrapMode  wrap_mode;
  gboolean     display_right_margin;
  guint        right_margin_position;

  ModelineSet  set;
} ModelineOptions;

void                   modeline_parser_init           (void);
void                   modeline_parser_shutdown       (void);
const ModelineOptions *modeline_parser_apply_modeline (GtkTextBuffer *buffer);

static inline gboolean
modeline_has_option (const ModelineOptions *options,
                     ModelineSet            set)
{
  return !!(options->set & set);
}

G_END_DECLS

/* ex:set ts=8 noet: */

