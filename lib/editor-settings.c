/*
 * editor-settings.c
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

#include "editor-document.h"
#include "editor-settings.h"
#include "editor-settings-addin-private.h"

#include "editor-gsettings-addin.h"
#ifdef HAVE_EDITORCONFIG
# include "editor-editorconfig-addin.h"
#endif

struct _EditorSettings
{
  GObject               parent_instance;

  GWeakRef              document_wr;

  GPtrArray            *addins;
  char                 *encoding;
  char                 *language;
  char                 *keybindings;
  char                 *syntax;
  GtkSourceSpaceDrawer *draw_spaces;

  GtkSourceNewlineType  newline_type;
  int                   indent_width;
  guint                 tab_width;
  guint                 right_margin_position;
  EditorZoom            zoom;
  double                line_height;

  guint                 auto_indent : 1;
  guint                 discover_settings : 1;
  guint                 enable_snippets : 1;
  guint                 enable_spellcheck : 1;
  guint                 highlight_current_line : 1;
  guint                 highlight_matching_brackets : 1;
  guint                 implicit_trailing_newline : 1;
  guint                 insert_spaces_instead_of_tabs : 1;
  guint                 show_line_numbers : 1;
  guint                 show_right_margin : 1;
  guint                 wrap_text : 1;

  guint                 auto_indent_set : 1;
  guint                 discover_settings_set : 1;
  guint                 draw_spaces_set : 1;
  guint                 enable_snippets_set : 1;
  guint                 enable_spellcheck_set : 1;
  guint                 encoding_set : 1;
  guint                 highlight_current_line_set : 1;
  guint                 highlight_matching_brackets_set : 1;
  guint                 indent_width_set : 1;
  guint                 implicit_trailing_newline_set : 1;
  guint                 insert_spaces_instead_of_tabs_set : 1;
  guint                 keybindings_set : 1;
  guint                 language_set : 1;
  guint                 line_height_set : 1;
  guint                 newline_type_set : 1;
  guint                 right_margin_position_set : 1;
  guint                 show_right_margin_set : 1;
  guint                 show_line_numbers_set : 1;
  guint                 syntax_set : 1;
  guint                 tab_width_set : 1;
  guint                 wrap_text_set : 1;
  guint                 zoom_set : 1;
};

G_DEFINE_FINAL_TYPE (EditorSettings, editor_settings, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_AUTO_INDENT,
  PROP_DISCOVER_SETTINGS,
  PROP_DOCUMENT,
  PROP_DRAW_SPACES,
  PROP_ENABLE_SNIPPETS,
  PROP_ENABLE_SPELLCHECK,
  PROP_ENCODING,
  PROP_HIGHLIGHT_CURRENT_LINE,
  PROP_HIGHLIGHT_MATCHING_BRACKETS,
  PROP_IMPLICIT_TRAILING_NEWLINE,
  PROP_INDENT_WIDTH,
  PROP_INSERT_SPACES_INSTEAD_OF_TABS,
  PROP_KEYBINDINGS,
  PROP_LANGUAGE,
  PROP_LINE_HEIGHT,
  PROP_NEWLINE_TYPE,
  PROP_RIGHT_MARGIN_POSITION,
  PROP_SHOW_LINE_NUMBERS,
  PROP_SHOW_RIGHT_MARGIN,
  PROP_SYNTAX,
  PROP_TAB_WIDTH,
  PROP_WRAP_TEXT,
  PROP_ZOOM,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

EditorSettings *
editor_settings_new (EditorDocument *document)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (document), NULL);

  return g_object_new (EDITOR_TYPE_SETTINGS,
                       "document", document,
                       NULL);
}

static void
editor_settings_addin_changed_cb (EditorSettings       *self,
                                  EditorSettingsOption  option,
                                  EditorSettingsAddin  *addin)
{
  guint prop_id = 0;

  g_assert (EDITOR_IS_SETTINGS (self));
  g_assert (EDITOR_IS_SETTINGS_ADDIN (addin));

  switch (option)
    {
    case EDITOR_SETTINGS_OPTION_AUTO_INDENT:
      prop_id = PROP_AUTO_INDENT;
      break;

    case EDITOR_SETTINGS_OPTION_DISCOVER_SETTINGS:
      prop_id = PROP_DISCOVER_SETTINGS;
      break;

    case EDITOR_SETTINGS_OPTION_DRAW_SPACES:
      prop_id = PROP_DRAW_SPACES;
      break;

    case EDITOR_SETTINGS_OPTION_ENABLE_SPELLCHECK:
      prop_id = PROP_ENABLE_SPELLCHECK;
      break;

    case EDITOR_SETTINGS_OPTION_IMPLICIT_TRAILING_NEWLINE:
      prop_id = PROP_IMPLICIT_TRAILING_NEWLINE;
      break;

    case EDITOR_SETTINGS_OPTION_INSERT_SPACES_INSTEAD_OF_TABS:
      prop_id = PROP_INSERT_SPACES_INSTEAD_OF_TABS;
      break;

    case EDITOR_SETTINGS_OPTION_SHOW_LINE_NUMBERS:
      prop_id = PROP_SHOW_LINE_NUMBERS;
      break;

    case EDITOR_SETTINGS_OPTION_SHOW_RIGHT_MARGIN:
      prop_id = PROP_SHOW_RIGHT_MARGIN;
      break;

    case EDITOR_SETTINGS_OPTION_WRAP_TEXT:
      prop_id = PROP_WRAP_TEXT;
      break;

    case EDITOR_SETTINGS_OPTION_RIGHT_MARGIN_POSITION:
      prop_id = PROP_RIGHT_MARGIN_POSITION;
      break;

    case EDITOR_SETTINGS_OPTION_TAB_WIDTH:
      prop_id = PROP_TAB_WIDTH;
      break;

    case EDITOR_SETTINGS_OPTION_INDENT_WIDTH:
      prop_id = PROP_INDENT_WIDTH;
      break;

    case EDITOR_SETTINGS_OPTION_ENCODING:
      prop_id = PROP_ENCODING;
      break;

    case EDITOR_SETTINGS_OPTION_LANGUAGE:
      prop_id = PROP_LANGUAGE;
      break;

    case EDITOR_SETTINGS_OPTION_SYNTAX:
      prop_id = PROP_SYNTAX;
      break;

    case EDITOR_SETTINGS_OPTION_NEWLINE_TYPE:
      prop_id = PROP_NEWLINE_TYPE;
      break;

    case EDITOR_SETTINGS_OPTION_ZOOM:
      prop_id = PROP_ZOOM;
      break;

    case EDITOR_SETTINGS_OPTION_HIGHLIGHT_CURRENT_LINE:
      prop_id = PROP_HIGHLIGHT_CURRENT_LINE;
      break;

    case EDITOR_SETTINGS_OPTION_HIGHLIGHT_MATCHING_BRACKETS:
      prop_id = PROP_HIGHLIGHT_MATCHING_BRACKETS;
      break;

    case EDITOR_SETTINGS_OPTION_ENABLE_SNIPPETS:
      prop_id = PROP_ENABLE_SNIPPETS;
      break;

    case EDITOR_SETTINGS_OPTION_LINE_HEIGHT:
      prop_id = PROP_LINE_HEIGHT;
      break;

    case EDITOR_SETTINGS_OPTION_KEYBINDINGS:
      prop_id = PROP_KEYBINDINGS;
      break;

    default:
      g_return_if_reached ();
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[prop_id]);
}

static void
editor_settings_append_addin (EditorSettings      *self,
                              EditorSettingsAddin *addin)
{
  g_assert (EDITOR_IS_SETTINGS (self));
  g_assert (EDITOR_IS_SETTINGS_ADDIN (addin));

  g_signal_connect_object (addin,
                           "changed",
                           G_CALLBACK (editor_settings_addin_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_ptr_array_add (self->addins, g_steal_pointer (&addin));
}

static void
editor_settings_constructed (GObject *object)
{
  EditorSettings *self = (EditorSettings *)object;
  g_autoptr(EditorSettingsAddin) gsettings = NULL;

  G_OBJECT_CLASS (editor_settings_parent_class)->constructed (object);

#if HAVE_EDITORCONFIG
  editor_settings_append_addin (self, editor_editorconfig_addin_new ());
#endif
  editor_settings_append_addin (self, editor_gsettings_addin_new ());
}

static void
editor_settings_finalize (GObject *object)
{
  EditorSettings *self = (EditorSettings *)object;

  g_weak_ref_clear (&self->document_wr);

  g_clear_pointer (&self->addins, g_ptr_array_unref);
  g_clear_pointer (&self->encoding, g_free);
  g_clear_pointer (&self->language, g_free);
  g_clear_pointer (&self->keybindings, g_free);
  g_clear_pointer (&self->syntax, g_free);
  g_clear_object (&self->draw_spaces);

  G_OBJECT_CLASS (editor_settings_parent_class)->finalize (object);
}

static void
editor_settings_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EditorSettings *self = EDITOR_SETTINGS (object);

  switch (prop_id)
    {
    case PROP_AUTO_INDENT:
      g_value_set_boolean (value, editor_settings_get_auto_indent (self));
      break;

    case PROP_DISCOVER_SETTINGS:
      g_value_set_boolean (value, editor_settings_get_discover_settings (self));
      break;

    case PROP_DOCUMENT:
      g_value_take_object (value, editor_settings_dup_document (self));
      break;

    case PROP_DRAW_SPACES:
      g_value_take_object (value, editor_settings_dup_draw_spaces (self));
      break;

    case PROP_ENABLE_SNIPPETS:
      g_value_set_boolean (value, editor_settings_get_enable_snippets (self));
      break;

    case PROP_ENABLE_SPELLCHECK:
      g_value_set_boolean (value, editor_settings_get_enable_spellcheck (self));
      break;

    case PROP_ENCODING:
      g_value_take_string (value, editor_settings_dup_encoding (self));
      break;

    case PROP_HIGHLIGHT_CURRENT_LINE:
      g_value_set_boolean (value, editor_settings_get_highlight_current_line (self));
      break;

    case PROP_HIGHLIGHT_MATCHING_BRACKETS:
      g_value_set_boolean (value, editor_settings_get_highlight_matching_brackets (self));
      break;

    case PROP_IMPLICIT_TRAILING_NEWLINE:
      g_value_set_boolean (value, editor_settings_get_implicit_trailing_newline (self));
      break;

    case PROP_INDENT_WIDTH:
      g_value_set_int (value, editor_settings_get_indent_width (self));
      break;

    case PROP_INSERT_SPACES_INSTEAD_OF_TABS:
      g_value_set_boolean (value, editor_settings_get_insert_spaces_instead_of_tabs (self));
      break;

    case PROP_KEYBINDINGS:
      g_value_take_string (value, editor_settings_dup_keybindings (self));
      break;

    case PROP_LANGUAGE:
      g_value_take_string (value, editor_settings_dup_language (self));
      break;

    case PROP_LINE_HEIGHT:
      g_value_set_double (value, editor_settings_get_line_height (self));
      break;

    case PROP_NEWLINE_TYPE:
      g_value_set_enum (value, editor_settings_get_newline_type (self));
      break;

    case PROP_RIGHT_MARGIN_POSITION:
      g_value_set_uint (value, editor_settings_get_right_margin_position (self));
      break;

    case PROP_SHOW_RIGHT_MARGIN:
      g_value_set_uint (value, editor_settings_get_show_right_margin (self));
      break;

    case PROP_SHOW_LINE_NUMBERS:
      g_value_set_boolean (value, editor_settings_get_show_line_numbers (self));
      break;

    case PROP_SYNTAX:
      g_value_take_string (value, editor_settings_dup_syntax (self));
      break;

    case PROP_TAB_WIDTH:
      g_value_set_uint (value, editor_settings_get_tab_width (self));
      break;

    case PROP_WRAP_TEXT:
      g_value_set_boolean (value, editor_settings_get_wrap_text (self));
      break;

    case PROP_ZOOM:
      g_value_set_enum (value, editor_settings_get_zoom (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_settings_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  EditorSettings *self = EDITOR_SETTINGS (object);

  switch (prop_id)
    {
    case PROP_AUTO_INDENT:
      editor_settings_set_auto_indent (self, g_value_get_boolean (value));
      break;

    case PROP_DISCOVER_SETTINGS:
      editor_settings_set_discover_settings (self, g_value_get_boolean (value));
      break;

    case PROP_DOCUMENT:
      g_weak_ref_set (&self->document_wr, g_value_get_object (value));
      break;

    case PROP_DRAW_SPACES:
      editor_settings_set_draw_spaces (self, g_value_get_object (value));
      break;

    case PROP_ENABLE_SNIPPETS:
      editor_settings_set_enable_snippets (self, g_value_get_boolean (value));
      break;

    case PROP_ENABLE_SPELLCHECK:
      editor_settings_set_enable_spellcheck (self, g_value_get_boolean (value));
      break;

    case PROP_ENCODING:
      editor_settings_set_encoding (self, g_value_get_string (value));
      break;

    case PROP_HIGHLIGHT_CURRENT_LINE:
      editor_settings_set_highlight_current_line (self, g_value_get_boolean (value));
      break;

    case PROP_HIGHLIGHT_MATCHING_BRACKETS:
      editor_settings_set_highlight_matching_brackets (self, g_value_get_boolean (value));
      break;

    case PROP_IMPLICIT_TRAILING_NEWLINE:
      editor_settings_set_implicit_trailing_newline (self, g_value_get_boolean (value));
      break;

    case PROP_INDENT_WIDTH:
      editor_settings_set_indent_width (self, g_value_get_int (value));
      break;

    case PROP_INSERT_SPACES_INSTEAD_OF_TABS:
      editor_settings_set_insert_spaces_instead_of_tabs (self, g_value_get_boolean (value));
      break;

    case PROP_KEYBINDINGS:
      editor_settings_set_keybindings (self, g_value_get_string (value));
      break;

    case PROP_LANGUAGE:
      editor_settings_set_language (self, g_value_get_string (value));
      break;

    case PROP_LINE_HEIGHT:
      editor_settings_set_line_height (self, g_value_get_double (value));
      break;

    case PROP_NEWLINE_TYPE:
      editor_settings_set_newline_type (self, g_value_get_enum (value));
      break;

    case PROP_RIGHT_MARGIN_POSITION:
      editor_settings_set_right_margin_position (self, g_value_get_uint (value));
      break;

    case PROP_SHOW_LINE_NUMBERS:
      editor_settings_set_show_line_numbers (self, g_value_get_boolean (value));
      break;

    case PROP_SHOW_RIGHT_MARGIN:
      editor_settings_set_show_right_margin (self, g_value_get_boolean (value));
      break;

    case PROP_SYNTAX:
      editor_settings_set_syntax (self, g_value_get_string (value));
      break;

    case PROP_TAB_WIDTH:
      editor_settings_set_tab_width (self, g_value_get_uint (value));
      break;

    case PROP_WRAP_TEXT:
      editor_settings_set_wrap_text (self, g_value_get_boolean (value));
      break;

    case PROP_ZOOM:
      editor_settings_set_zoom (self, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_settings_class_init (EditorSettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = editor_settings_constructed;
  object_class->finalize = editor_settings_finalize;
  object_class->get_property = editor_settings_get_property;
  object_class->set_property = editor_settings_set_property;

  properties[PROP_AUTO_INDENT] =
    g_param_spec_boolean ("auto-indent", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_DISCOVER_SETTINGS] =
    g_param_spec_boolean ("discover-settings", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         EDITOR_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_DRAW_SPACES] =
    g_param_spec_object ("draw-spaces", NULL, NULL,
                         GTK_SOURCE_TYPE_SPACE_DRAWER,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_ENABLE_SPELLCHECK] =
    g_param_spec_boolean ("enable-spellcheck", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_ENABLE_SNIPPETS] =
    g_param_spec_boolean ("enable-snippets", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_ENCODING] =
    g_param_spec_string ("encoding", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_HIGHLIGHT_CURRENT_LINE] =
    g_param_spec_boolean ("highlight-current-line", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_HIGHLIGHT_MATCHING_BRACKETS] =
    g_param_spec_boolean ("highlight-matching-brackets", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_IMPLICIT_TRAILING_NEWLINE] =
    g_param_spec_boolean ("implicit-trailing-newline", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_INDENT_WIDTH] =
    g_param_spec_int ("indent-width", NULL, NULL,
                      -1, 32, -1,
                      (G_PARAM_READWRITE |
                       G_PARAM_EXPLICIT_NOTIFY |
                       G_PARAM_STATIC_STRINGS));

  properties[PROP_INSERT_SPACES_INSTEAD_OF_TABS] =
    g_param_spec_object ("insert-spaces-instead-of-tabs", NULL, NULL,
                         FALSE,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_LANGUAGE] =
    g_param_spec_string ("language", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_NEWLINE_TYPE] =
    g_param_spec_enum ("newline-type", NULL, NULL,
                       GTK_SOURCE_TYPE_NEWLINE_TYPE,
                       GTK_SOURCE_NEWLINE_TYPE_LF,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_RIGHT_MARGIN_POSITION] =
    g_param_spec_uint ("right-margin-position", NULL, NULL,
                       1, 1000, 80,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_SHOW_LINE_NUMBERS] =
    g_param_spec_boolean ("show-line-numbers", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_SHOW_RIGHT_MARGIN] =
    g_param_spec_boolean ("show-right-margin", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_SYNTAX] =
    g_param_spec_string ("syntax", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_TAB_WIDTH] =
    g_param_spec_uint ("tab-width", NULL, NULL,
                       1, 32, 8,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_WRAP_TEXT] =
    g_param_spec_boolean ("wrap-text", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_ZOOM] =
    g_param_spec_enum ("zoom", NULL, NULL,
                       EDITOR_TYPE_ZOOM,
                       EDITOR_ZOOM_DEFAULT,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_settings_init (EditorSettings *self)
{
  g_weak_ref_init (&self->document_wr, NULL);

  self->addins = g_ptr_array_new_with_free_func (g_object_unref);

  self->auto_indent = TRUE;
  self->keybindings = g_strdup ("default");
  self->discover_settings = TRUE;
  self->enable_spellcheck = TRUE;
  self->implicit_trailing_newline = TRUE;
  self->indent_width = -1;
  self->right_margin_position = 80;
  self->tab_width = 8;
  self->wrap_text = TRUE;
  self->highlight_matching_brackets = TRUE;
  self->line_height = 1.2;
}

static gboolean
editor_settings_get_boolean (EditorSettings       *self,
                             EditorSettingsOption  option,
                             gboolean              current_value,
                             gboolean              current_value_set)
{
  g_auto(GValue) value = G_VALUE_INIT;

  g_assert (EDITOR_IS_SETTINGS (self));

  if (current_value_set)
    return current_value;

  g_value_init (&value, G_TYPE_BOOLEAN);

  for (guint i = 0; i < self->addins->len; i++)
    {
      EditorSettingsAddin *addin = g_ptr_array_index (self->addins, i);

      if (editor_settings_addin_get_value (addin, option, &value))
        return g_value_get_boolean (&value);
    }

  return current_value;
}

static int
editor_settings_get_double (EditorSettings       *self,
                            EditorSettingsOption  option,
                            double                current_value,
                            gboolean              current_value_set)
{
  g_auto(GValue) value = G_VALUE_INIT;

  g_assert (EDITOR_IS_SETTINGS (self));

  if (current_value_set)
    return current_value;

  g_value_init (&value, G_TYPE_DOUBLE);

  for (guint i = 0; i < self->addins->len; i++)
    {
      EditorSettingsAddin *addin = g_ptr_array_index (self->addins, i);

      if (editor_settings_addin_get_value (addin, option, &value))
        return g_value_get_double (&value);
    }

  return current_value;
}

static int
editor_settings_get_int (EditorSettings       *self,
                         EditorSettingsOption  option,
                         int                   current_value,
                         gboolean              current_value_set)
{
  g_auto(GValue) value = G_VALUE_INIT;

  g_assert (EDITOR_IS_SETTINGS (self));

  if (current_value_set)
    return current_value;

  g_value_init (&value, G_TYPE_INT);

  for (guint i = 0; i < self->addins->len; i++)
    {
      EditorSettingsAddin *addin = g_ptr_array_index (self->addins, i);

      if (editor_settings_addin_get_value (addin, option, &value))
        return g_value_get_int (&value);
    }

  return current_value;
}

static guint
editor_settings_get_uint (EditorSettings       *self,
                          EditorSettingsOption  option,
                          guint                 current_value,
                          gboolean              current_value_set)
{
  g_auto(GValue) value = G_VALUE_INIT;

  g_assert (EDITOR_IS_SETTINGS (self));

  if (current_value_set)
    return current_value;

  g_value_init (&value, G_TYPE_UINT);

  for (guint i = 0; i < self->addins->len; i++)
    {
      EditorSettingsAddin *addin = g_ptr_array_index (self->addins, i);

      if (editor_settings_addin_get_value (addin, option, &value))
        return g_value_get_uint (&value);
    }

  return current_value;
}

static char *
editor_settings_dup_string (EditorSettings       *self,
                            EditorSettingsOption  option,
                            const char           *current_value,
                            gboolean              current_value_set)
{
  g_auto(GValue) value = G_VALUE_INIT;

  g_assert (EDITOR_IS_SETTINGS (self));

  if (current_value_set)
    return g_strdup (current_value);

  g_value_init (&value, G_TYPE_STRING);

  for (guint i = 0; i < self->addins->len; i++)
    {
      EditorSettingsAddin *addin = g_ptr_array_index (self->addins, i);

      if (editor_settings_addin_get_value (addin, option, &value))
        return g_value_dup_string (&value);
    }

  return g_strdup (current_value);
}

static gpointer
editor_settings_dup_object (EditorSettings       *self,
                            EditorSettingsOption  option,
                            GType                 type,
                            gpointer              current_value,
                            gboolean              current_value_set)
{
  g_auto(GValue) value = G_VALUE_INIT;

  g_assert (EDITOR_IS_SETTINGS (self));

  if (current_value_set)
    return current_value ? g_object_ref (current_value) : NULL;

  g_value_init (&value, type);

  for (guint i = 0; i < self->addins->len; i++)
    {
      EditorSettingsAddin *addin = g_ptr_array_index (self->addins, i);

      if (editor_settings_addin_get_value (addin, option, &value))
        return g_value_dup_object (&value);
    }

  return current_value ? g_object_ref (current_value) : NULL;
}

static guint
editor_settings_get_enum (EditorSettings       *self,
                          EditorSettingsOption  option,
                          GType                 enum_type,
                          guint                 current_value,
                          gboolean              current_value_set)
{
  g_auto(GValue) value = G_VALUE_INIT;

  g_assert (EDITOR_IS_SETTINGS (self));
  g_assert (G_TYPE_IS_ENUM (enum_type));

  if (current_value_set)
    return current_value;

  g_value_init (&value, enum_type);

  for (guint i = 0; i < self->addins->len; i++)
    {
      EditorSettingsAddin *addin = g_ptr_array_index (self->addins, i);

      if (editor_settings_addin_get_value (addin, option, &value))
        return g_value_get_enum (&value);
    }

  return current_value;
}

gboolean
editor_settings_get_discover_settings (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), FALSE);

  return self->discover_settings;
}

void
editor_settings_set_discover_settings (EditorSettings *self,
                                       gboolean        discover_settings)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  discover_settings = !!discover_settings;

  if (self->discover_settings != discover_settings)
    {
      self->discover_settings = discover_settings;
      self->discover_settings_set = TRUE;

      /* Notify all properties when changing this property because
       * it will change what layer may be queried.
       */
      for (guint i = 1; i < N_PROPS; i++)
        g_object_notify_by_pspec (G_OBJECT (self), properties[i]);
    }
}

gboolean
editor_settings_get_auto_indent (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), FALSE);

  return editor_settings_get_boolean (self,
                                      EDITOR_SETTINGS_OPTION_AUTO_INDENT,
                                      self->auto_indent,
                                      self->auto_indent_set);
}

void
editor_settings_set_auto_indent (EditorSettings *self,
                                 gboolean        auto_indent)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  auto_indent = !!auto_indent;

  if (auto_indent != self->auto_indent)
    {
      self->auto_indent = auto_indent;
      self->auto_indent_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_INDENT]);
    }
}

gboolean
editor_settings_get_enable_spellcheck (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), FALSE);

  return editor_settings_get_boolean (self,
                                      EDITOR_SETTINGS_OPTION_ENABLE_SPELLCHECK,
                                      self->enable_spellcheck,
                                      self->enable_spellcheck_set);
}

void
editor_settings_set_enable_spellcheck (EditorSettings *self,
                                       gboolean        enable_spellcheck)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  enable_spellcheck = !!enable_spellcheck;

  if (enable_spellcheck != self->enable_spellcheck)
    {
      self->enable_spellcheck = enable_spellcheck;
      self->enable_spellcheck_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLE_SPELLCHECK]);
    }
}

gboolean
editor_settings_get_enable_snippets (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), FALSE);

  return editor_settings_get_boolean (self,
                                      EDITOR_SETTINGS_OPTION_ENABLE_SNIPPETS,
                                      self->enable_snippets,
                                      self->enable_snippets_set);
}

void
editor_settings_set_enable_snippets (EditorSettings *self,
                                     gboolean        enable_snippets)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  enable_snippets = !!enable_snippets;

  if (enable_snippets != self->enable_snippets)
    {
      self->enable_snippets = enable_snippets;
      self->enable_snippets_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLE_SNIPPETS]);
    }
}

int
editor_settings_get_indent_width (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), -1);

  return editor_settings_get_int (self,
                                  EDITOR_SETTINGS_OPTION_INDENT_WIDTH,
                                  self->indent_width,
                                  self->indent_width_set);
}

void
editor_settings_set_indent_width (EditorSettings *self,
                                  int             indent_width)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  if (indent_width != self->indent_width)
    {
      self->indent_width = indent_width;
      self->indent_width_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INDENT_WIDTH]);
    }
}

guint
editor_settings_get_tab_width (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), -1);

  return editor_settings_get_uint (self,
                                   EDITOR_SETTINGS_OPTION_TAB_WIDTH,
                                   self->tab_width,
                                   self->tab_width_set);
}

void
editor_settings_set_tab_width (EditorSettings *self,
                               guint           tab_width)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  if (tab_width != self->tab_width)
    {
      self->tab_width = tab_width;
      self->tab_width_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TAB_WIDTH]);
    }
}

char *
editor_settings_dup_encoding (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), NULL);

  return editor_settings_dup_string (self,
                                     EDITOR_SETTINGS_OPTION_ENCODING,
                                     self->encoding,
                                     self->encoding_set);
}

void
editor_settings_set_encoding (EditorSettings *self,
                              const char     *encoding)
{
  if (g_set_str (&self->encoding, encoding))
    {
      self->encoding_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENCODING]);
    }
}

char *
editor_settings_dup_syntax (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), NULL);

  return editor_settings_dup_string (self,
                                     EDITOR_SETTINGS_OPTION_SYNTAX,
                                     self->syntax,
                                     self->syntax_set);
}

void
editor_settings_set_syntax (EditorSettings *self,
                            const char     *syntax)
{
  if (g_set_str (&self->syntax, syntax))
    {
      self->syntax_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SYNTAX]);
    }
}

char *
editor_settings_dup_language (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), NULL);

  return editor_settings_dup_string (self,
                                     EDITOR_SETTINGS_OPTION_LANGUAGE,
                                     self->language,
                                     self->language_set);
}

void
editor_settings_set_language (EditorSettings *self,
                              const char     *language)
{
  if (g_set_str (&self->language, language))
    {
      self->language_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LANGUAGE]);
    }
}

char *
editor_settings_dup_keybindings (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), NULL);

  return editor_settings_dup_string (self,
                                     EDITOR_SETTINGS_OPTION_KEYBINDINGS,
                                     self->keybindings,
                                     self->keybindings_set);
}

void
editor_settings_set_keybindings (EditorSettings *self,
                                 const char     *keybindings)
{
  if (g_set_str (&self->keybindings, keybindings))
    {
      self->keybindings_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KEYBINDINGS]);
    }
}

GtkSourceNewlineType
editor_settings_get_newline_type (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), FALSE);

  return editor_settings_get_enum (self,
                                   EDITOR_SETTINGS_OPTION_NEWLINE_TYPE,
                                   GTK_SOURCE_TYPE_NEWLINE_TYPE,
                                   self->newline_type,
                                   self->newline_type_set);
}

void
editor_settings_set_newline_type (EditorSettings       *self,
                                  GtkSourceNewlineType  newline_type)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  if (newline_type != self->newline_type)
    {
      self->newline_type = newline_type;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NEWLINE_TYPE]);
    }
}

guint
editor_settings_get_right_margin_position (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), -1);

  return editor_settings_get_uint (self,
                                   EDITOR_SETTINGS_OPTION_RIGHT_MARGIN_POSITION,
                                   self->right_margin_position,
                                   self->right_margin_position_set);
}

void
editor_settings_set_right_margin_position (EditorSettings *self,
                                           guint           right_margin_position)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  if (right_margin_position != self->right_margin_position)
    {
      self->right_margin_position = right_margin_position;
      self->right_margin_position_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RIGHT_MARGIN_POSITION]);
    }
}

gboolean
editor_settings_get_show_right_margin (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), FALSE);

  return editor_settings_get_boolean (self,
                                      EDITOR_SETTINGS_OPTION_SHOW_RIGHT_MARGIN,
                                      self->show_right_margin,
                                      self->show_right_margin_set);
}

void
editor_settings_set_show_right_margin (EditorSettings *self,
                                       gboolean        show_right_margin)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  show_right_margin = !!show_right_margin;

  if (show_right_margin != self->show_right_margin)
    {
      self->show_right_margin = show_right_margin;
      self->show_right_margin_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_RIGHT_MARGIN]);
    }
}

gboolean
editor_settings_get_insert_spaces_instead_of_tabs (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), FALSE);

  return editor_settings_get_boolean (self,
                                      EDITOR_SETTINGS_OPTION_INSERT_SPACES_INSTEAD_OF_TABS,
                                      self->insert_spaces_instead_of_tabs,
                                      self->insert_spaces_instead_of_tabs_set);
}

void
editor_settings_set_insert_spaces_instead_of_tabs (EditorSettings *self,
                                                   gboolean        insert_spaces_instead_of_tabs)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  insert_spaces_instead_of_tabs = !!insert_spaces_instead_of_tabs;

  if (insert_spaces_instead_of_tabs != self->insert_spaces_instead_of_tabs)
    {
      self->insert_spaces_instead_of_tabs = insert_spaces_instead_of_tabs;
      self->insert_spaces_instead_of_tabs_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INSERT_SPACES_INSTEAD_OF_TABS]);
    }
}

gboolean
editor_settings_get_show_line_numbers (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), FALSE);

  return editor_settings_get_boolean (self,
                                      EDITOR_SETTINGS_OPTION_SHOW_LINE_NUMBERS,
                                      self->show_line_numbers,
                                      self->show_line_numbers_set);
}

void
editor_settings_set_show_line_numbers (EditorSettings *self,
                                       gboolean        show_line_numbers)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  show_line_numbers = !!show_line_numbers;

  if (show_line_numbers != self->show_line_numbers)
    {
      self->show_line_numbers = show_line_numbers;
      self->show_line_numbers_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_LINE_NUMBERS]);
    }
}

gboolean
editor_settings_get_wrap_text (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), FALSE);

  return editor_settings_get_boolean (self,
                                      EDITOR_SETTINGS_OPTION_WRAP_TEXT,
                                      self->wrap_text,
                                      self->wrap_text_set);
}

void
editor_settings_set_wrap_text (EditorSettings *self,
                               gboolean        wrap_text)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  wrap_text = !!wrap_text;

  if (wrap_text != self->wrap_text)
    {
      self->wrap_text = wrap_text;
      self->wrap_text_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WRAP_TEXT]);
    }
}

gboolean
editor_settings_get_highlight_current_line (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), FALSE);

  return editor_settings_get_boolean (self,
                                      EDITOR_SETTINGS_OPTION_HIGHLIGHT_CURRENT_LINE,
                                      self->highlight_current_line,
                                      self->highlight_current_line_set);
}

void
editor_settings_set_highlight_current_line (EditorSettings *self,
                                            gboolean        highlight_current_line)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  highlight_current_line = !!highlight_current_line;

  if (highlight_current_line != self->highlight_current_line)
    {
      self->highlight_current_line = highlight_current_line;
      self->highlight_current_line_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIGHLIGHT_CURRENT_LINE]);
    }
}

gboolean
editor_settings_get_highlight_matching_brackets (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), FALSE);

  return editor_settings_get_boolean (self,
                                      EDITOR_SETTINGS_OPTION_HIGHLIGHT_MATCHING_BRACKETS,
                                      self->highlight_matching_brackets,
                                      self->highlight_matching_brackets_set);
}

void
editor_settings_set_highlight_matching_brackets (EditorSettings *self,
                                                 gboolean        highlight_matching_brackets)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));

  highlight_matching_brackets = !!highlight_matching_brackets;

  if (highlight_matching_brackets != self->highlight_matching_brackets)
    {
      self->highlight_matching_brackets = highlight_matching_brackets;
      self->highlight_matching_brackets_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIGHLIGHT_MATCHING_BRACKETS]);
    }
}

GtkSourceSpaceDrawer *
editor_settings_dup_draw_spaces (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), NULL);

  return editor_settings_dup_object (self,
                                     EDITOR_SETTINGS_OPTION_DRAW_SPACES,
                                     GTK_SOURCE_TYPE_SPACE_DRAWER,
                                     self->draw_spaces,
                                     self->draw_spaces_set);
}

void
editor_settings_set_draw_spaces (EditorSettings       *self,
                                 GtkSourceSpaceDrawer *draw_spaces)
{
  g_return_if_fail (EDITOR_IS_SETTINGS (self));
  g_return_if_fail (GTK_SOURCE_IS_SPACE_DRAWER (draw_spaces));

  if (g_set_object (&self->draw_spaces, draw_spaces))
    {
      self->draw_spaces_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DRAW_SPACES]);
    }
}

double
editor_settings_get_line_height (EditorSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS (self), .0);

  return editor_settings_get_double (self,
                                     EDITOR_SETTINGS_OPTION_LINE_HEIGHT,
                                     self->line_height,
                                     self->line_height_set);
}

DexFuture *
editor_settings_load (EditorSettings *self,
                      GFile          *file)
{
  g_autoptr(EditorDocument) document = NULL;
  g_autoptr(GPtrArray) futures = NULL;

  dex_return_error_if_fail (EDITOR_IS_SETTINGS (self));
  dex_return_error_if_fail (G_IS_FILE (file));

  document = editor_settings_dup_document (self);
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (document));

  futures = g_ptr_array_new_with_free_func (dex_unref);

  for (guint i = 0; i < self->addins->len; i++)
    g_ptr_array_add (futures,
                     editor_settings_addin_load (g_ptr_array_index (self->addins, i),
                                                 document, file));


  if (futures->len == 0)
    return dex_future_new_true ();

  return dex_future_anyv ((DexFuture **)futures->pdata, futures->len);
}

DexFuture *
editor_settings_save (EditorSettings *self,
                      GFile          *file)
{
  dex_return_error_if_fail (EDITOR_IS_SETTINGS (self));
  dex_return_error_if_fail (G_IS_FILE (file));

  /* Save metadata to GFile attributes */

  return dex_future_new_true ();
}
