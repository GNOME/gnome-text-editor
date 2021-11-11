/* editor-modeline-settings-provider.c
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

#define G_LOG_DOMAIN "editor-modeline-settings-provider"

#include "config.h"

#include "editor-document.h"
#include "editor-modeline-settings-provider-private.h"

#include "modeline-parser.h"

struct _EditorModelineSettingsProvider
{
  GObject         parent_instance;

  EditorDocument *document;

  guint           reload_source;

  guint           tab_width;
  int             indent_width;
  guint           right_margin_position;

  guint           tab_width_set : 1;
  guint           indent_width_set : 1;
  guint           wrap_text : 1;
  guint           wrap_text_set : 1;
  guint           right_margin_position_set : 1;
  guint           show_right_margin : 1;
  guint           show_right_margin_set : 1;
  guint           insert_spaces_instead_of_tabs : 1;
  guint           insert_spaces_instead_of_tabs_set : 1;
};

static gboolean
editor_modeline_settings_provider_get_tab_width (EditorPageSettingsProvider *provider,
                                                 guint                      *tab_width)
{
  EditorModelineSettingsProvider *self = EDITOR_MODELINE_SETTINGS_PROVIDER (provider);
  *tab_width = self->tab_width;
  return self->tab_width_set;
}

static gboolean
editor_modeline_settings_provider_get_indent_width (EditorPageSettingsProvider *provider,
                                                    int                        *indent_width)
{
  EditorModelineSettingsProvider *self = EDITOR_MODELINE_SETTINGS_PROVIDER (provider);
  *indent_width = self->indent_width;
  return self->indent_width_set;
}

static gboolean
editor_modeline_settings_provider_get_right_margin_position (EditorPageSettingsProvider *provider,
                                                             guint                      *right_margin_position)
{
  EditorModelineSettingsProvider *self = EDITOR_MODELINE_SETTINGS_PROVIDER (provider);
  *right_margin_position = self->right_margin_position;
  return self->right_margin_position_set;
}

static gboolean
editor_modeline_settings_provider_get_wrap_text (EditorPageSettingsProvider *provider,
                                                 gboolean                   *wrap_text)
{
  EditorModelineSettingsProvider *self = EDITOR_MODELINE_SETTINGS_PROVIDER (provider);
  *wrap_text = self->wrap_text;
  return self->wrap_text_set;
}

static gboolean
editor_modeline_settings_provider_get_show_right_margin (EditorPageSettingsProvider *provider,
                                                         gboolean                   *show_right_margin)
{
  EditorModelineSettingsProvider *self = EDITOR_MODELINE_SETTINGS_PROVIDER (provider);
  *show_right_margin = self->show_right_margin;
  return self->show_right_margin_set;
}

static gboolean
editor_modeline_settings_provider_get_insert_spaces_instead_of_tabs (EditorPageSettingsProvider *provider,
                                                                     gboolean                   *insert_spaces_instead_of_tabs)
{
  EditorModelineSettingsProvider *self = EDITOR_MODELINE_SETTINGS_PROVIDER (provider);
  *insert_spaces_instead_of_tabs = self->insert_spaces_instead_of_tabs;
  return self->insert_spaces_instead_of_tabs_set;
}

static gboolean
editor_modeline_settings_provider_reload (gpointer data)
{
  EditorModelineSettingsProvider *self = data;

  g_assert (EDITOR_IS_MODELINE_SETTINGS_PROVIDER (self));

  self->reload_source = 0;

  if (self->document != NULL)
    {
      const ModelineOptions *options;

      if ((options = modeline_parser_apply_modeline (GTK_TEXT_BUFFER (self->document))))
        {
          if (modeline_has_option (options, MODELINE_SET_LANGUAGE))
            {
              if (g_strcmp0 (options->language_id, "text") == 0)
                {
                  gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (self->document), NULL);
                }
              else
                {
                  GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();
                  GtkSourceLanguage *l = gtk_source_language_manager_get_language (lm, options->language_id);

                  if (l != NULL)
                    gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (self->document), l);
                }
            }

          if ((self->tab_width_set = modeline_has_option (options, MODELINE_SET_TAB_WIDTH)))
            self->tab_width = options->tab_width;

          if (modeline_has_option (options, MODELINE_SET_INDENT_WIDTH))
            {
              if (options->indent_width >= 1 && options->indent_width <= 32)
                {
                  self->indent_width = options->indent_width;
                  self->indent_width_set = TRUE;
                }
            }

          if ((self->wrap_text_set = modeline_has_option (options, MODELINE_SET_WRAP_MODE)))
            self->wrap_text = options->wrap_mode != GTK_WRAP_NONE;

          if ((self->right_margin_position_set = modeline_has_option (options, MODELINE_SET_RIGHT_MARGIN_POSITION)))
            self->right_margin_position = options->right_margin_position;

          if ((self->insert_spaces_instead_of_tabs_set = modeline_has_option (options, MODELINE_SET_INSERT_SPACES)))
            self->insert_spaces_instead_of_tabs = options->insert_spaces;

          if ((self->show_right_margin_set = modeline_has_option (options, MODELINE_SET_SHOW_RIGHT_MARGIN)))
            self->show_right_margin = options->display_right_margin;

          editor_page_settings_provider_emit_changed (EDITOR_PAGE_SETTINGS_PROVIDER (self));
        }
    }

  return G_SOURCE_REMOVE;
}

static void
editor_modeline_settings_provider_queue_reload (EditorModelineSettingsProvider *self)
{
  g_assert (EDITOR_IS_MODELINE_SETTINGS_PROVIDER (self));

  /* This will get called a lot, so to avoid churning GSource on the
   * main context, we instead simply change the ready time of the
   * GSource to be the current monotonic time + 1 second.
   */

  if (self->reload_source != 0)
    {
      GSource *source = g_main_context_find_source_by_id (NULL, self->reload_source);

      if (source != NULL)
        {
          g_source_set_ready_time (source, g_get_monotonic_time () + G_USEC_PER_SEC);
          return;
        }

      g_clear_handle_id (&self->reload_source, g_source_remove);
    }

  self->reload_source =
    g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
                                1,
                                editor_modeline_settings_provider_reload,
                                g_object_ref (self),
                                g_object_unref);
}

static void
editor_modeline_settings_provider_set_document (EditorPageSettingsProvider *provider,
                                                EditorDocument             *document)
{
  EditorModelineSettingsProvider *self = (EditorModelineSettingsProvider *)provider;

  g_assert (EDITOR_IS_MODELINE_SETTINGS_PROVIDER (self));
  g_assert (!document || EDITOR_IS_DOCUMENT (document));

  if (g_set_weak_pointer (&self->document, document))
    {
      g_signal_connect_object (document,
                               "changed",
                               G_CALLBACK (editor_modeline_settings_provider_queue_reload),
                               self,
                               G_CONNECT_SWAPPED);
      g_clear_handle_id (&self->reload_source, g_source_remove);
      editor_modeline_settings_provider_reload (self);
    }
}

static void
page_settings_provider_iface_init (EditorPageSettingsProviderInterface *iface)
{
  iface->set_document = editor_modeline_settings_provider_set_document;
  iface->get_tab_width = editor_modeline_settings_provider_get_tab_width;
  iface->get_indent_width = editor_modeline_settings_provider_get_indent_width;
  iface->get_wrap_text = editor_modeline_settings_provider_get_wrap_text;
  iface->get_right_margin_position = editor_modeline_settings_provider_get_right_margin_position;
  iface->get_show_right_margin = editor_modeline_settings_provider_get_show_right_margin;
  iface->get_insert_spaces_instead_of_tabs = editor_modeline_settings_provider_get_insert_spaces_instead_of_tabs;
}

G_DEFINE_TYPE_WITH_CODE (EditorModelineSettingsProvider, editor_modeline_settings_provider, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (EDITOR_TYPE_PAGE_SETTINGS_PROVIDER, page_settings_provider_iface_init))

/**
 * _editor_modeline_settings_provider_new:
 *
 * Create a new #EditorModelineSettingsProvider.
 *
 * Returns: (transfer full): a newly created #EditorModelineSettingsProvider
 */
EditorPageSettingsProvider *
_editor_modeline_settings_provider_new (void)
{
  return g_object_new (EDITOR_TYPE_MODELINE_SETTINGS_PROVIDER, NULL);
}

static void
editor_modeline_settings_provider_dispose (GObject *object)
{
  EditorModelineSettingsProvider *self = (EditorModelineSettingsProvider *)object;

  g_clear_weak_pointer (&self->document);
  g_clear_handle_id (&self->reload_source, g_source_remove);

  G_OBJECT_CLASS (editor_modeline_settings_provider_parent_class)->dispose (object);
}

static void
editor_modeline_settings_provider_class_init (EditorModelineSettingsProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_modeline_settings_provider_dispose;

  modeline_parser_init ();
}

static void
editor_modeline_settings_provider_init (EditorModelineSettingsProvider *self)
{
  self->tab_width = 8;
  self->right_margin_position = 80;
}
