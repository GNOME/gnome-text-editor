/* editor-spell-cursor.c
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

#include "config.h"

#include "editor-spell-cursor.h"

static char *
editor_spell_cursor_word (EditorSpellCursor *cursor)
{
  g_assert (cursor != NULL);
  g_assert (!cursor->exhausted);

  return gtk_text_iter_get_slice (&cursor->word_begin, &cursor->word_end);
}

void
editor_spell_cursor_init (EditorSpellCursor *cursor,
                          const GtkTextIter *begin,
                          const GtkTextIter *end,
                          GtkTextTag        *misspelled_tag)
{
  g_return_if_fail (cursor != NULL);
  g_return_if_fail (begin != NULL);
  g_return_if_fail (end != NULL);
  g_return_if_fail (gtk_text_iter_get_buffer (begin) == gtk_text_iter_get_buffer (end));

  cursor->buffer = gtk_text_iter_get_buffer (begin);
  cursor->misspelled_tag = misspelled_tag;
  cursor->begin = *begin;
  cursor->end = *end;
  gtk_text_iter_order (&cursor->begin, &cursor->end);
  cursor->word_begin = cursor->begin;
  cursor->word_end = cursor->begin;
  cursor->exhausted = FALSE;
}

char *
editor_spell_cursor_next_word (EditorSpellCursor *cursor)
{
  g_return_val_if_fail (cursor != NULL, NULL);

  if (cursor->exhausted)
    return NULL;

  /* If this is the initial movement, then we need to handle the
   * case where the first word overlaps the boundary.
   */
  if (gtk_text_iter_equal (&cursor->word_begin, &cursor->word_end))
    {
      if (!gtk_text_iter_starts_word (&cursor->word_begin))
        {
          if (gtk_text_iter_backward_word_start (&cursor->word_begin))
            {
              cursor->word_end = cursor->word_begin;
              gtk_text_iter_forward_word_end (&cursor->word_end);
            }
           else
            {
              if (!gtk_text_iter_forward_word_end (&cursor->word_end))
                goto exhausted;

              cursor->word_begin = cursor->word_end;
              gtk_text_iter_backward_word_start (&cursor->word_begin);
            }
        }

      /* If the word position overlaps the region, then we can return it.
       * Otherwise we need to try to move forward (the regular flow).
       */
      if (gtk_text_iter_compare (&cursor->word_end, &cursor->begin) > 0)
        return editor_spell_cursor_word (cursor);
    }

  if (!gtk_text_iter_forward_word_end (&cursor->word_end))
    goto exhausted;

  cursor->word_begin = cursor->word_end;
  gtk_text_iter_backward_word_start (&cursor->word_begin);

  if (gtk_text_iter_compare (&cursor->word_begin, &cursor->end) <= 0)
    return editor_spell_cursor_word (cursor);

exhausted:
  cursor->exhausted = TRUE;

  return NULL;
}

void
editor_spell_cursor_tag (EditorSpellCursor *cursor)
{
  g_return_if_fail (cursor != NULL);

  gtk_text_buffer_apply_tag (cursor->buffer,
                             cursor->misspelled_tag,
                             &cursor->word_begin,
                             &cursor->word_end);
}

gboolean
editor_spell_cursor_contains_tag (EditorSpellCursor *cursor,
                                  GtkTextTag        *tag)
{
  GtkTextIter toggle_iter;

  if (tag == NULL || cursor->exhausted)
    return FALSE;

  if (gtk_text_iter_has_tag (&cursor->word_begin, tag))
    return TRUE;

  toggle_iter = cursor->word_begin;
  if (!gtk_text_iter_forward_to_tag_toggle (&toggle_iter, tag))
    return FALSE;

  return gtk_text_iter_compare (&cursor->word_end, &toggle_iter) > 0;
}
