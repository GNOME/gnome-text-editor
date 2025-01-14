/*
 * editor-editorconfig-addin.c
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

#include "editorconfig-glib.h"
#include "editor-document.h"
#include "editor-editorconfig-addin.h"

struct _EditorEditorconfigAddin
{
  EditorSettingsAddin parent_instance;

  int indent_width;
  guint right_margin_position;
  guint tab_width;

  guint implicit_trailing_newline : 1;
  guint implicit_trailing_newline_set : 1;
  guint indent_width_set : 1;
  guint insert_spaces_instead_of_tabs : 1;
  guint insert_spaces_instead_of_tabs_set : 1;
  guint right_margin_position_set : 1;
  guint tab_width_set : 1;
};

G_DEFINE_FINAL_TYPE (EditorEditorconfigAddin, editor_editorconfig_addin, EDITOR_TYPE_SETTINGS_ADDIN)

typedef struct _Load
{
  EditorDocument *document;
  GFile *file;
} Load;

static void
load_free (Load *state)
{
  g_clear_object (&state->document);
  g_clear_object (&state->file);
  g_free (state);
}

static DexFuture *
editor_editorconfig_addin_load_fiber (gpointer data)
{
  Load *state = data;
  g_autoptr(GHashTable) ht = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (state != NULL);
  g_assert (EDITOR_IS_DOCUMENT (state->document));
  g_assert (G_IS_FILE (state->file));

  if (!(ht = editorconfig_glib_read (state->file, NULL, &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));
  else
    return dex_future_new_take_boxed (G_TYPE_HASH_TABLE, g_steal_pointer (&ht));
}

static DexFuture *
apply_settings_cb (DexFuture *completed,
                   gpointer   user_data)
{
  EditorEditorconfigAddin *self = user_data;
  g_autoptr(GHashTable) ht = NULL;

  g_assert (EDITOR_IS_EDITORCONFIG_ADDIN (self));
  g_assert (DEX_IS_FUTURE (completed));

  self->implicit_trailing_newline_set = FALSE;
  self->indent_width_set = FALSE;
  self->insert_spaces_instead_of_tabs_set = FALSE;
  self->right_margin_position_set = FALSE;
  self->tab_width_set = FALSE;

  if ((ht = dex_await_boxed (dex_ref (completed), NULL)))
    {
      GHashTableIter iter;
      gpointer k, v;

      g_hash_table_iter_init (&iter, ht);

      while (g_hash_table_iter_next (&iter, &k, &v))
        {
          const gchar *key = k;
          const GValue *value = v;

          if (g_str_equal (key, "tab_width"))
            {
              self->tab_width = g_value_get_int (value);
              self->tab_width_set = TRUE;
            }
          else if (g_str_equal (key, "max_line_length"))
            {
              self->right_margin_position = g_value_get_int (value);
              self->right_margin_position_set = TRUE;
            }
          else if (g_str_equal (key, "indent_style"))
            {
              const gchar *str = g_value_get_string (value);
              self->insert_spaces_instead_of_tabs = g_strcmp0 (str, "tab") != 0;
              self->insert_spaces_instead_of_tabs_set = TRUE;
            }
          else if (g_str_equal (key, "indent_size"))
            {
              self->indent_width = g_value_get_int (value);
              self->indent_width_set = TRUE;
            }
          else if (g_str_equal (key, "insert_final_newline"))
            {
              self->implicit_trailing_newline = g_value_get_boolean (value);
              self->implicit_trailing_newline_set = TRUE;
            }
        }
    }

  editor_settings_addin_changed (EDITOR_SETTINGS_ADDIN (self),
                                 EDITOR_SETTINGS_OPTION_IMPLICIT_TRAILING_NEWLINE);
  editor_settings_addin_changed (EDITOR_SETTINGS_ADDIN (self),
                                 EDITOR_SETTINGS_OPTION_INDENT_WIDTH);
  editor_settings_addin_changed (EDITOR_SETTINGS_ADDIN (self),
                                 EDITOR_SETTINGS_OPTION_INSERT_SPACES_INSTEAD_OF_TABS);
  editor_settings_addin_changed (EDITOR_SETTINGS_ADDIN (self),
                                 EDITOR_SETTINGS_OPTION_RIGHT_MARGIN_POSITION);
  editor_settings_addin_changed (EDITOR_SETTINGS_ADDIN (self),
                                 EDITOR_SETTINGS_OPTION_TAB_WIDTH);

  return dex_future_new_true ();
}

static DexFuture *
editor_editorconfig_addin_load (EditorSettingsAddin *settings_addin,
                                EditorDocument      *document,
                                GFile               *file)
{
  Load *state;

  g_assert (EDITOR_IS_EDITORCONFIG_ADDIN (settings_addin));
  g_assert (EDITOR_IS_DOCUMENT (document));
  g_assert (!file || G_IS_FILE (file));

  if (file == NULL)
    return dex_future_new_true ();

  state = g_new0 (Load, 1);
  g_set_object (&state->document, document);
  g_set_object (&state->file, file);

  return dex_future_then (dex_scheduler_spawn (dex_thread_pool_scheduler_get_default (), 0,
                                               editor_editorconfig_addin_load_fiber,
                                               state,
                                               (GDestroyNotify) load_free),
                          apply_settings_cb,
                          g_object_ref (settings_addin),
                          g_object_unref);
}

static gboolean
editor_editorconfig_addin_get_value (EditorSettingsAddin  *settings_addin,
                                     EditorSettingsOption  option,
                                     GValue               *value)
{
  EditorEditorconfigAddin *self = EDITOR_EDITORCONFIG_ADDIN (settings_addin);

  switch (option)
    {
    case EDITOR_SETTINGS_OPTION_IMPLICIT_TRAILING_NEWLINE:
      if (self->implicit_trailing_newline_set)
        {
          g_value_set_boolean (value, self->implicit_trailing_newline);
          return TRUE;
        }
      break;

    case EDITOR_SETTINGS_OPTION_INDENT_WIDTH:
      if (self->indent_width_set)
        {
          g_value_set_int (value, self->indent_width);
          return TRUE;
        }
      break;

    case EDITOR_SETTINGS_OPTION_INSERT_SPACES_INSTEAD_OF_TABS:
      if (self->insert_spaces_instead_of_tabs_set)
        {
          g_value_set_boolean (value, self->insert_spaces_instead_of_tabs);
          return TRUE;
        }
      break;

    case EDITOR_SETTINGS_OPTION_RIGHT_MARGIN_POSITION:
      if (self->right_margin_position_set)
        {
          g_value_set_uint (value, self->right_margin_position);
          return TRUE;
        }
      break;

    case EDITOR_SETTINGS_OPTION_TAB_WIDTH:
      if (self->tab_width_set)
        {
          g_value_set_uint (value, self->tab_width);
          return TRUE;
        }
      break;

    case EDITOR_SETTINGS_OPTION_AUTO_INDENT:
    case EDITOR_SETTINGS_OPTION_DISCOVER_SETTINGS:
    case EDITOR_SETTINGS_OPTION_DRAW_SPACES:
    case EDITOR_SETTINGS_OPTION_ENABLE_SNIPPETS:
    case EDITOR_SETTINGS_OPTION_ENABLE_SPELLCHECK:
    case EDITOR_SETTINGS_OPTION_ENCODING:
    case EDITOR_SETTINGS_OPTION_HIGHLIGHT_CURRENT_LINE:
    case EDITOR_SETTINGS_OPTION_HIGHLIGHT_MATCHING_BRACKETS:
    case EDITOR_SETTINGS_OPTION_KEYBINDINGS:
    case EDITOR_SETTINGS_OPTION_LANGUAGE:
    case EDITOR_SETTINGS_OPTION_LINE_HEIGHT:
    case EDITOR_SETTINGS_OPTION_NEWLINE_TYPE:
    case EDITOR_SETTINGS_OPTION_SHOW_LINE_NUMBERS:
    case EDITOR_SETTINGS_OPTION_SHOW_RIGHT_MARGIN:
    case EDITOR_SETTINGS_OPTION_SYNTAX:
    case EDITOR_SETTINGS_OPTION_WRAP_TEXT:
    case EDITOR_SETTINGS_OPTION_ZOOM:
      break;

    default:
      g_return_val_if_reached (FALSE);
    }

  return FALSE;
}

static void
editor_editorconfig_addin_class_init (EditorEditorconfigAddinClass *klass)
{
  EditorSettingsAddinClass *settings_addin_class = EDITOR_SETTINGS_ADDIN_CLASS (klass);

  settings_addin_class->load = editor_editorconfig_addin_load;
  settings_addin_class->get_value = editor_editorconfig_addin_get_value;
}

static void
editor_editorconfig_addin_init (EditorEditorconfigAddin *self)
{
}
