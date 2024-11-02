/* editor-page-settings.c
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

#define G_LOG_DOMAIN "editor-page-settings"

#include "config.h"

#include "editor-document.h"
#include "editor-enums.h"
#include "editor-page-gsettings-private.h"
#include "editor-page-settings.h"
#include "editor-page-settings-provider.h"

#include "defaults/editor-page-defaults-private.h"
#include "modelines/editor-modeline-settings-provider-private.h"

#ifdef HAVE_EDITORCONFIG
# include "editorconfig/editor-page-editorconfig-private.h"
#endif

struct _EditorPageSettings
{
  GObject parent_instance;

  GSettings *settings;
  EditorDocument *document;
  GPtrArray *providers;

  gchar *custom_font;
  gchar *style_scheme;
  gchar *style_variant;

  guint right_margin_position;
  guint tab_width;
  int indent_width;

  guint highlight_current_line : 1;
  guint highlight_matching_brackets : 1;
  guint implicit_trailing_newline : 1;
  guint insert_spaces_instead_of_tabs : 1;
  guint show_line_numbers : 1;
  guint show_grid : 1;
  guint show_map : 1;
  guint show_right_margin : 1;
  guint use_system_font : 1;
  guint wrap_text : 1;
  guint auto_indent : 1;

  guint custom_font_set : 1;
  guint style_scheme_set : 1;
  guint style_variant_set : 1;
  guint right_margin_position_set : 1;
  guint tab_width_set : 1;
  guint indent_width_set : 1;
  guint highlight_current_line_set : 1;
  guint highlight_matching_brackets_set : 1;
  guint implicit_trailing_newline_set : 1;
  guint insert_spaces_instead_of_tabs_set : 1;
  guint show_line_numbers_set : 1;
  guint show_grid_set : 1;
  guint show_map_set : 1;
  guint show_right_margin_set : 1;
  guint use_system_font_set : 1;
  guint wrap_text_set : 1;
  guint auto_indent_set : 1;
};

enum {
  PROP_0,
  PROP_AUTO_INDENT,
  PROP_CUSTOM_FONT,
  PROP_STYLE_VARIANT,
  PROP_DOCUMENT,
  PROP_HIGHLIGHT_CURRENT_LINE,
  PROP_HIGHLIGHT_MATCHING_BRACKETS,
  PROP_IMPLICIT_TRAILING_NEWLINE,
  PROP_INDENT_WIDTH,
  PROP_INDENT_STYLE,
  PROP_INSERT_SPACES_INSTEAD_OF_TABS,
  PROP_RIGHT_MARGIN_POSITION,
  PROP_SHOW_GRID,
  PROP_SHOW_LINE_NUMBERS,
  PROP_SHOW_MAP,
  PROP_SHOW_RIGHT_MARGIN,
  PROP_STYLE_SCHEME,
  PROP_TAB_WIDTH,
  PROP_USE_SYSTEM_FONT,
  PROP_WRAP_TEXT,
  N_PROPS
};

G_DEFINE_TYPE (EditorPageSettings, editor_page_settings, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static inline gboolean
cmp_boolean (gboolean a,
             gboolean b)
{
  return a == b;
}

static inline gboolean
cmp_string (const gchar *a,
            const gchar *b)
{
  return g_strcmp0 (a, b) == 0;
}

static inline gboolean
cmp_uint (guint a,
          guint b)
{
  return a == b;
}

static inline gboolean
cmp_int (int a,
         int b)
{
  return a == b;
}

static void
editor_page_settings_update (EditorPageSettings *self)
{
  g_assert (EDITOR_IS_PAGE_SETTINGS (self));

#define UPDATE_SETTING(type, name, NAME, cmp, free_func, dup_func)                    \
  G_STMT_START {                                                                      \
    type name = 0;                                                                    \
    if (self->name##_set)                                                             \
      {                                                                               \
        g_debug ("ignoring providers for %s from user override", #name);              \
        break;                                                                        \
      }                                                                               \
    for (guint i = 0; i < self->providers->len; i++)                                  \
      {                                                                               \
        EditorPageSettingsProvider *p = g_ptr_array_index (self->providers, i);       \
        if (editor_page_settings_provider_get_##name (p, &name))                      \
          {                                                                           \
            if (!cmp (self->name, name))                                              \
              {                                                                       \
                free_func (self->name);                                               \
                self->name = dup_func (name);                                         \
                g_debug ("using %s from %s", #name, G_OBJECT_TYPE_NAME (p));          \
                g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_##NAME]); \
              }                                                                       \
            break;                                                                    \
          }                                                                           \
      }                                                                               \
  } G_STMT_END

  UPDATE_SETTING (gboolean, insert_spaces_instead_of_tabs, INSERT_SPACES_INSTEAD_OF_TABS, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, show_line_numbers, SHOW_LINE_NUMBERS, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, show_grid, SHOW_GRID, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, show_map, SHOW_MAP, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, show_right_margin, SHOW_RIGHT_MARGIN, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, highlight_current_line, HIGHLIGHT_CURRENT_LINE, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, highlight_matching_brackets, HIGHLIGHT_MATCHING_BRACKETS, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, use_system_font, USE_SYSTEM_FONT, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, wrap_text, WRAP_TEXT, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, auto_indent, AUTO_INDENT, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, implicit_trailing_newline, IMPLICIT_TRAILING_NEWLINE, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (guint, tab_width, TAB_WIDTH, cmp_uint, (void), (guint));
  UPDATE_SETTING (int, indent_width, INDENT_WIDTH, cmp_int, (void), (int));
  UPDATE_SETTING (guint, right_margin_position, RIGHT_MARGIN_POSITION, cmp_uint, (void), (guint));
  UPDATE_SETTING (g_autofree gchar *, custom_font, CUSTOM_FONT, cmp_string, g_free, g_strdup);
  UPDATE_SETTING (g_autofree gchar *, style_scheme, STYLE_SCHEME, cmp_string, g_free, g_strdup);
  UPDATE_SETTING (g_autofree gchar *, style_variant, STYLE_VARIANT, cmp_string, g_free, g_strdup);

#undef UPDATE_SETTING_BOOL
}

static void
take_provider (EditorPageSettings         *self,
               EditorPageSettingsProvider *provider)
{
  g_assert (EDITOR_IS_PAGE_SETTINGS (self));
  g_assert (EDITOR_IS_PAGE_SETTINGS_PROVIDER (provider));

  g_ptr_array_add (self->providers, provider);

  g_signal_connect_object (provider,
                           "changed",
                           G_CALLBACK (editor_page_settings_update),
                           self,
                           G_CONNECT_SWAPPED);

  editor_page_settings_provider_set_document (provider, self->document);

  editor_page_settings_update (self);
}

static void
editor_page_settings_changed_discover_settings_cb (EditorPageSettings *self,
                                                   GParamSpec         *pspec,
                                                   GSettings          *settings)
{
  g_assert (EDITOR_IS_PAGE_SETTINGS (self));
  g_assert (G_IS_SETTINGS (settings));

  /* Remove all the providers */
  for (guint i = self->providers->len; i > 0; i--)
    {
      EditorPageSettingsProvider *provider = g_ptr_array_index (self->providers, i - 1);
      g_signal_handlers_disconnect_by_func (provider,
                                            G_CALLBACK (editor_page_settings_update),
                                            self);
      g_ptr_array_remove_index (self->providers, i - 1);
    }

  /* Now add providers based on settings */
  if (g_settings_get_boolean (settings, "discover-settings"))
    {
      take_provider (self, _editor_modeline_settings_provider_new ());
#ifdef HAVE_EDITORCONFIG
      take_provider (self, _editor_page_editorconfig_new ());
#endif
      take_provider (self, _editor_page_defaults_new ());
    }

  take_provider (self, _editor_page_gsettings_new (self->settings));

  editor_page_settings_update (self);
}

static void
editor_page_settings_constructed (GObject *object)
{
  EditorPageSettings *self = (EditorPageSettings *)object;

  g_assert (EDITOR_IS_PAGE_SETTINGS (self));

  G_OBJECT_CLASS (editor_page_settings_parent_class)->constructed (object);

  editor_page_settings_changed_discover_settings_cb (self, NULL, self->settings);
}

static void
editor_page_settings_dispose (GObject *object)
{
  EditorPageSettings *self = (EditorPageSettings *)object;

  g_assert (EDITOR_IS_PAGE_SETTINGS (self));

  for (guint i = self->providers->len; i > 0; i--)
    {
      EditorPageSettingsProvider *provider = g_ptr_array_index (self->providers, i - 1);
      g_signal_handlers_disconnect_by_func (provider,
                                            G_CALLBACK (editor_page_settings_update),
                                            self);
      g_ptr_array_remove_index (self->providers, i - 1);
    }

  g_clear_object (&self->settings);
  g_clear_weak_pointer (&self->document);

  G_OBJECT_CLASS (editor_page_settings_parent_class)->dispose (object);
}

static void
editor_page_settings_finalize (GObject *object)
{
  EditorPageSettings *self = (EditorPageSettings *)object;

  g_clear_pointer (&self->providers, g_ptr_array_unref);
  g_clear_pointer (&self->custom_font, g_free);
  g_clear_pointer (&self->style_scheme, g_free);

  G_OBJECT_CLASS (editor_page_settings_parent_class)->finalize (object);
}

static void
editor_page_settings_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  EditorPageSettings *self = EDITOR_PAGE_SETTINGS (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_set_object (value, self->document);
      break;

    case PROP_CUSTOM_FONT:
      g_value_set_string (value, editor_page_settings_get_custom_font (self));
      break;

    case PROP_STYLE_SCHEME:
      g_value_set_string (value, editor_page_settings_get_style_scheme (self));
      break;

    case PROP_STYLE_VARIANT:
      g_value_set_string (value, editor_page_settings_get_style_variant (self));
      break;

    case PROP_HIGHLIGHT_CURRENT_LINE:
      g_value_set_boolean (value, editor_page_settings_get_highlight_current_line (self));
      break;

    case PROP_HIGHLIGHT_MATCHING_BRACKETS:
      g_value_set_boolean (value, editor_page_settings_get_highlight_matching_brackets (self));
      break;

    case PROP_INSERT_SPACES_INSTEAD_OF_TABS:
      g_value_set_boolean (value, editor_page_settings_get_insert_spaces_instead_of_tabs (self));
      break;

    case PROP_INDENT_STYLE:
      g_value_set_enum (value, !!editor_page_settings_get_insert_spaces_instead_of_tabs (self));
      break;

    case PROP_RIGHT_MARGIN_POSITION:
      g_value_set_uint (value, editor_page_settings_get_right_margin_position (self));
      break;

    case PROP_SHOW_LINE_NUMBERS:
      g_value_set_boolean (value, editor_page_settings_get_show_line_numbers (self));
      break;

    case PROP_SHOW_GRID:
      g_value_set_boolean (value, editor_page_settings_get_show_grid (self));
      break;

    case PROP_SHOW_MAP:
      g_value_set_boolean (value, editor_page_settings_get_show_map (self));
      break;

    case PROP_SHOW_RIGHT_MARGIN:
      g_value_set_boolean (value, editor_page_settings_get_show_right_margin (self));
      break;

    case PROP_TAB_WIDTH:
      g_value_set_uint (value, editor_page_settings_get_tab_width (self));
      break;

    case PROP_IMPLICIT_TRAILING_NEWLINE:
      g_value_set_boolean (value, editor_page_settings_get_implicit_trailing_newline (self));
      break;

    case PROP_INDENT_WIDTH:
      g_value_set_int (value, editor_page_settings_get_indent_width (self));
      break;

    case PROP_USE_SYSTEM_FONT:
      g_value_set_boolean (value, editor_page_settings_get_use_system_font (self));
      break;

    case PROP_WRAP_TEXT:
      g_value_set_boolean (value, editor_page_settings_get_wrap_text (self));
      break;

    case PROP_AUTO_INDENT:
      g_value_set_boolean (value, editor_page_settings_get_auto_indent (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_page_settings_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  EditorPageSettings *self = EDITOR_PAGE_SETTINGS (object);

  switch (prop_id)
    {
    case PROP_AUTO_INDENT:
      self->auto_indent = g_value_get_boolean (value);
      self->auto_indent_set = TRUE;
      break;

    case PROP_DOCUMENT:
      g_set_weak_pointer (&self->document, g_value_get_object (value));
      break;

    case PROP_CUSTOM_FONT:
      g_free (self->custom_font);
      self->custom_font = g_value_dup_string (value);
      self->custom_font_set = TRUE;
      break;

    case PROP_RIGHT_MARGIN_POSITION:
      self->right_margin_position = g_value_get_uint (value);
      self->right_margin_position_set = TRUE;
      break;

    case PROP_STYLE_SCHEME:
      g_free (self->style_scheme);
      self->style_scheme = g_value_dup_string (value);
      self->style_scheme_set = TRUE;
      break;

    case PROP_STYLE_VARIANT:
      g_free (self->style_variant);
      self->style_variant = g_value_dup_string (value);
      self->style_variant_set = TRUE;
      break;

    case PROP_HIGHLIGHT_CURRENT_LINE:
      self->highlight_current_line = g_value_get_boolean (value);
      self->highlight_current_line_set = TRUE;
      break;

    case PROP_HIGHLIGHT_MATCHING_BRACKETS:
      self->highlight_matching_brackets = g_value_get_boolean (value);
      self->highlight_matching_brackets_set = TRUE;
      break;

    case PROP_INSERT_SPACES_INSTEAD_OF_TABS:
      self->insert_spaces_instead_of_tabs = g_value_get_boolean (value);
      self->insert_spaces_instead_of_tabs_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_INDENT_STYLE]);
      break;

    case PROP_IMPLICIT_TRAILING_NEWLINE:
      self->implicit_trailing_newline = g_value_get_boolean (value);
      self->implicit_trailing_newline_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_IMPLICIT_TRAILING_NEWLINE]);
      break;

    case PROP_INDENT_STYLE:
      self->insert_spaces_instead_of_tabs = !!g_value_get_enum (value);
      self->insert_spaces_instead_of_tabs_set = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_INSERT_SPACES_INSTEAD_OF_TABS]);
      break;

    case PROP_SHOW_LINE_NUMBERS:
      self->show_line_numbers = g_value_get_boolean (value);
      self->show_line_numbers_set = TRUE;
      break;

    case PROP_SHOW_GRID:
      self->show_grid = g_value_get_boolean (value);
      self->show_grid_set = TRUE;
      break;

    case PROP_SHOW_MAP:
      self->show_map = g_value_get_boolean (value);
      self->show_map_set = TRUE;
      break;

    case PROP_SHOW_RIGHT_MARGIN:
      self->show_right_margin = g_value_get_boolean (value);
      self->show_right_margin_set = TRUE;
      break;

    case PROP_TAB_WIDTH:
      self->tab_width = g_value_get_uint (value);
      self->tab_width_set = TRUE;
      break;

    case PROP_INDENT_WIDTH:
      self->indent_width = g_value_get_int (value);
      self->indent_width_set = TRUE;
      break;

    case PROP_USE_SYSTEM_FONT:
      self->use_system_font = g_value_get_boolean (value);
      self->use_system_font_set = TRUE;
      break;

    case PROP_WRAP_TEXT:
      self->wrap_text = g_value_get_boolean (value);
      self->wrap_text_set = TRUE;
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_page_settings_class_init (EditorPageSettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = editor_page_settings_constructed;
  object_class->dispose = editor_page_settings_dispose;
  object_class->finalize = editor_page_settings_finalize;
  object_class->get_property = editor_page_settings_get_property;
  object_class->set_property = editor_page_settings_set_property;

  properties [PROP_CUSTOM_FONT] =
    g_param_spec_string ("custom-font",
                         "Custom Font",
                         "The custom font to use in the page",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_STYLE_SCHEME] =
    g_param_spec_string ("style-scheme",
                         "Style Scheme",
                         "The style scheme to use in the page",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_STYLE_VARIANT] =
    g_param_spec_string ("style-variant",
                         "Style Variant",
                         "The style variant to use (such as light or dark)",
                         "light",
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DOCUMENT] =
    g_param_spec_object ("document",
                         "Document",
                         "The document to be edited",
                         EDITOR_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_HIGHLIGHT_CURRENT_LINE] =
    g_param_spec_boolean ("highlight-current-line",
                          "Highlight Current Line",
                          "Highlight Current Line",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_HIGHLIGHT_MATCHING_BRACKETS] =
    g_param_spec_boolean ("highlight-matching-brackets", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_INSERT_SPACES_INSTEAD_OF_TABS] =
    g_param_spec_boolean ("insert-spaces-instead-of-tabs",
                          "Insert Spaces Instead of Tabs",
                          "If spaces should be inserted instead of tabs",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_INDENT_STYLE] =
    g_param_spec_enum ("indent-style", NULL, NULL,
                       EDITOR_TYPE_INDENT_STYLE,
                       EDITOR_INDENT_STYLE_TAB,
                       (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_RIGHT_MARGIN_POSITION] =
    g_param_spec_uint ("right-margin-position",
                       "Right Margin Position",
                       "The position for the right margin",
                       1, 1000, 80,
                       (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SHOW_LINE_NUMBERS] =
    g_param_spec_boolean ("show-line-numbers",
                          "Show Line Numbers",
                          "If line numbers should be displayed",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SHOW_GRID] =
    g_param_spec_boolean ("show-grid",
                          "Show Grid",
                          "If the blueprint grid should be displayed",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SHOW_MAP] =
    g_param_spec_boolean ("show-map",
                          "Show Overview Map",
                          "If the overview map should be displayed",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SHOW_RIGHT_MARGIN] =
    g_param_spec_boolean ("show-right-margin",
                          "Show Right Margin",
                          "If the right margin should be displayed",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TAB_WIDTH] =
    g_param_spec_uint ("tab-width",
                       "Tab Width",
                       "The tab width to use",
                       1, 32, 8,
                       (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_IMPLICIT_TRAILING_NEWLINE] =
    g_param_spec_boolean ("implicit-trailing-newline", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_INDENT_WIDTH] =
    g_param_spec_int ("indent-width",
                      "Indent Width",
                      "The width to use for indentation, or -1 to use tab width",
                      -1, 32, -1,
                      (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_USE_SYSTEM_FONT] =
    g_param_spec_boolean ("use-system-font",
                          "Use System Font",
                          "If the system monospace font should be used",
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_WRAP_TEXT] =
    g_param_spec_boolean ("wrap-text",
                          "Wrap Text",
                          "If the text should wrap",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_AUTO_INDENT] =
    g_param_spec_boolean ("auto-indent",
                          "Auto Indent",
                          "Automatically indent new lines by copying the previous line's indentation.",
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_page_settings_init (EditorPageSettings *self)
{
  self->providers = g_ptr_array_new_with_free_func (g_object_unref);
  self->auto_indent = TRUE;
  self->use_system_font = TRUE;
  self->highlight_matching_brackets = TRUE;
  self->right_margin_position = 80;
  self->tab_width = 8;
  self->indent_width = -1;
  self->implicit_trailing_newline = TRUE;
  self->settings = g_settings_new ("org.gnome.TextEditor");

  g_signal_connect_object (self->settings,
                           "changed::discover-settings",
                           G_CALLBACK (editor_page_settings_changed_discover_settings_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

EditorPageSettings *
editor_page_settings_new_for_document (EditorDocument *document)
{
  return g_object_new (EDITOR_TYPE_PAGE_SETTINGS,
                       "document", document,
                       NULL);
}

const gchar *
editor_page_settings_get_custom_font (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), NULL);

  return self->custom_font;
}

const gchar *
editor_page_settings_get_style_scheme (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), NULL);

  return self->style_scheme;
}

const gchar *
editor_page_settings_get_style_variant (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->style_variant;
}

gboolean
editor_page_settings_get_insert_spaces_instead_of_tabs (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->insert_spaces_instead_of_tabs;
}

gboolean
editor_page_settings_get_show_line_numbers (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->show_line_numbers;
}

gboolean
editor_page_settings_get_show_grid (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->show_grid;
}

gboolean
editor_page_settings_get_show_map (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->show_map;
}

gboolean
editor_page_settings_get_show_right_margin (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->show_right_margin;
}

gboolean
editor_page_settings_get_use_system_font (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->use_system_font;
}

gboolean
editor_page_settings_get_wrap_text (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->wrap_text;
}

gboolean
editor_page_settings_get_auto_indent (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->auto_indent;
}

guint
editor_page_settings_get_right_margin_position (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), 0);

  return self->right_margin_position;
}

guint
editor_page_settings_get_tab_width (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), 0);

  return self->tab_width;
}

gboolean
editor_page_settings_get_implicit_trailing_newline (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->implicit_trailing_newline;
}

int
editor_page_settings_get_indent_width (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), -1);

  return self->indent_width;
}

gboolean
editor_page_settings_get_highlight_current_line (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->highlight_current_line;
}

gboolean
editor_page_settings_get_highlight_matching_brackets (EditorPageSettings *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS (self), FALSE);

  return self->highlight_matching_brackets;
}
