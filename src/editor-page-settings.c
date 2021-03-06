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
#include "editor-page-gsettings-private.h"
#include "editor-page-settings.h"
#include "editor-page-settings-provider.h"

#include "defaults/editor-page-defaults-private.h"
#include "editorconfig/editor-page-editorconfig-private.h"
#include "modelines/editor-modeline-settings-provider-private.h"

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

  guint insert_spaces_instead_of_tabs : 1;
  guint show_line_numbers : 1;
  guint show_map : 1;
  guint show_right_margin : 1;
  guint use_system_font : 1;
  guint wrap_text : 1;
};

enum {
  PROP_0,
  PROP_CUSTOM_FONT,
  PROP_STYLE_VARIANT,
  PROP_DOCUMENT,
  PROP_INSERT_SPACES_INSTEAD_OF_TABS,
  PROP_RIGHT_MARGIN_POSITION,
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

static void
editor_page_settings_update (EditorPageSettings *self)
{
  g_assert (EDITOR_IS_PAGE_SETTINGS (self));

#define UPDATE_SETTING(type, name, NAME, cmp, free_func, dup_func)                    \
  G_STMT_START {                                                                      \
    type name = 0;                                                                    \
    for (guint i = 0; i < self->providers->len; i++)                                  \
      {                                                                               \
        EditorPageSettingsProvider *p = g_ptr_array_index (self->providers, i);       \
        if (editor_page_settings_provider_get_##name (p, &name))                      \
          {                                                                           \
            if (!cmp (self->name,  name))                                             \
              {                                                                       \
                free_func (self->name);                                               \
                self->name = dup_func (name);                                         \
                g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_##NAME]); \
              }                                                                       \
            break;                                                                    \
          }                                                                           \
      }                                                                               \
  } G_STMT_END

  UPDATE_SETTING (gboolean, insert_spaces_instead_of_tabs, INSERT_SPACES_INSTEAD_OF_TABS, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, show_line_numbers, SHOW_LINE_NUMBERS, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, show_map, SHOW_MAP, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, show_right_margin, SHOW_RIGHT_MARGIN, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, use_system_font, USE_SYSTEM_FONT, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (gboolean, wrap_text, WRAP_TEXT, cmp_boolean, (void), (gboolean));
  UPDATE_SETTING (guint, tab_width, TAB_WIDTH, cmp_uint, (void), (guint));
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
      take_provider (self, _editor_page_editorconfig_new ());
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

    case PROP_INSERT_SPACES_INSTEAD_OF_TABS:
      g_value_set_boolean (value, editor_page_settings_get_insert_spaces_instead_of_tabs (self));
      break;

    case PROP_RIGHT_MARGIN_POSITION:
      g_value_set_uint (value, editor_page_settings_get_right_margin_position (self));
      break;

    case PROP_SHOW_LINE_NUMBERS:
      g_value_set_boolean (value, editor_page_settings_get_show_line_numbers (self));
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

    case PROP_USE_SYSTEM_FONT:
      g_value_set_boolean (value, editor_page_settings_get_use_system_font (self));
      break;

    case PROP_WRAP_TEXT:
      g_value_set_boolean (value, editor_page_settings_get_wrap_text (self));
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
    case PROP_DOCUMENT:
      g_set_weak_pointer (&self->document, g_value_get_object (value));
      break;

    case PROP_CUSTOM_FONT:
      g_free (self->custom_font);
      self->custom_font = g_value_dup_string (value);
      break;

    case PROP_RIGHT_MARGIN_POSITION:
      self->right_margin_position = g_value_get_uint (value);
      break;

    case PROP_STYLE_SCHEME:
      g_free (self->style_scheme);
      self->style_scheme = g_value_dup_string (value);
      break;

    case PROP_STYLE_VARIANT:
      g_free (self->style_variant);
      self->style_variant = g_value_dup_string (value);
      break;

    case PROP_INSERT_SPACES_INSTEAD_OF_TABS:
      self->insert_spaces_instead_of_tabs = g_value_get_boolean (value);
      break;

    case PROP_SHOW_LINE_NUMBERS:
      self->show_line_numbers = g_value_get_boolean (value);
      break;

    case PROP_SHOW_MAP:
      self->show_map = g_value_get_boolean (value);
      break;

    case PROP_SHOW_RIGHT_MARGIN:
      self->show_right_margin = g_value_get_boolean (value);
      break;

    case PROP_TAB_WIDTH:
      self->tab_width = g_value_get_uint (value);
      break;

    case PROP_USE_SYSTEM_FONT:
      self->use_system_font = g_value_get_boolean (value);
      break;

    case PROP_WRAP_TEXT:
      self->wrap_text = g_value_get_boolean (value);
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

  properties [PROP_INSERT_SPACES_INSTEAD_OF_TABS] =
    g_param_spec_boolean ("insert-spaces-instead-of-tabs",
                          "Insert Spaces Instead of Tabs",
                          "If spaces should be inserted instead of tabs",
                          FALSE,
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

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_page_settings_init (EditorPageSettings *self)
{
  self->providers = g_ptr_array_new_with_free_func (g_object_unref);
  self->use_system_font = TRUE;
  self->right_margin_position = 80;
  self->tab_width = 8;
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
