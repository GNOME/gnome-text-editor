/* editor-spell-cursor.h
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

typedef struct
{
  GtkTextBuffer *buffer;
  GtkTextTag    *misspelled_tag;
  GtkTextIter    begin;
  GtkTextIter    end;
  GtkTextIter    word_begin;
  GtkTextIter    word_end;
  guint          exhausted : 1;
} EditorSpellCursor;

void      editor_spell_cursor_init         (EditorSpellCursor *cursor,
                                            const GtkTextIter *begin,
                                            const GtkTextIter *end,
                                            GtkTextTag        *misspelled_tag);
char     *editor_spell_cursor_next_word    (EditorSpellCursor *cursor);
void      editor_spell_cursor_tag          (EditorSpellCursor *cursor);
gboolean  editor_spell_cursor_contains_tag (EditorSpellCursor *cursor,
                                            GtkTextTag        *tag);

G_END_DECLS
