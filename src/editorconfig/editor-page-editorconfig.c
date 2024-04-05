/* editor-page-editorconfig.c
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

#define G_LOG_DOMAIN "editor-page-editorconfig"

#include "config.h"

#include "editor-document.h"
#include "editor-page-editorconfig-private.h"
#include "editorconfig-glib.h"

struct _EditorPageEditorconfig
{
  GObject         parent_instance;

  EditorDocument *document;

  int             indent_width;
  guint           tab_width;
  guint           right_margin_position;
  guint           insert_spaces_instead_of_tabs : 1;
  guint           implicit_trailing_newline : 1;

  guint           indent_width_set : 1;
  guint           tab_width_set : 1;
  guint           right_margin_position_set : 1;
  guint           insert_spaces_instead_of_tabs_set : 1;
  guint           implicit_trailing_newline_set : 1;
};

static void
editor_page_editorconfig_reload (EditorPageEditorconfig *self)
{
  g_autoptr(GHashTable) ht = NULL;
  GHashTableIter iter;
  gpointer k, v;
  GFile *file;

  g_assert (EDITOR_IS_PAGE_EDITORCONFIG (self));

  /* TODO: We probably want to do this all async from a thread. */

  if (self->document == NULL ||
      !(file = editor_document_get_file (self->document)) ||
      !(ht = editorconfig_glib_read (file, NULL, NULL)))
    return;

  g_hash_table_iter_init (&iter, ht);

  self->tab_width_set = FALSE;
  self->insert_spaces_instead_of_tabs_set = FALSE;
  self->right_margin_position_set = FALSE;

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

  if (g_hash_table_size (ht) > 0)
    editor_page_settings_provider_emit_changed (EDITOR_PAGE_SETTINGS_PROVIDER (self));
}

static void
editor_page_editorconfig_file_changed_cb (EditorPageEditorconfig *self,
                                          GParamSpec             *pspec,
                                          EditorDocument         *document)
{
  GFile *file;

  g_assert (EDITOR_IS_PAGE_EDITORCONFIG (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  if ((file = editor_document_get_file (document)))
    editor_page_editorconfig_reload (self);
}

static void
editor_page_editorconfig_set_document (EditorPageSettingsProvider *provider,
                                       EditorDocument             *document)
{
  EditorPageEditorconfig *self = (EditorPageEditorconfig *)provider;

  g_assert (EDITOR_IS_PAGE_EDITORCONFIG (self));
  g_assert (!document || EDITOR_IS_DOCUMENT (document));

  if (g_set_weak_pointer (&self->document, document))
    {
      if (document != NULL)
        {
          g_signal_connect_object (document,
                                   "notify::file",
                                   G_CALLBACK (editor_page_editorconfig_file_changed_cb),
                                   self,
                                   G_CONNECT_SWAPPED);
          editor_page_editorconfig_file_changed_cb (self, NULL, document);
        }
    }
}

static gboolean
editor_page_editorconfig_get_right_margin_position (EditorPageSettingsProvider *provider,
                                                    guint                      *right_margin_position)
{
  EditorPageEditorconfig *self = EDITOR_PAGE_EDITORCONFIG (provider);
  *right_margin_position = self->right_margin_position;
  return self->right_margin_position_set;
}

static gboolean
editor_page_editorconfig_get_tab_width (EditorPageSettingsProvider *provider,
                                        guint                      *tab_width)
{
  EditorPageEditorconfig *self = EDITOR_PAGE_EDITORCONFIG (provider);
  *tab_width = self->tab_width;
  return self->tab_width_set;
}

static gboolean
editor_page_editorconfig_get_indent_width (EditorPageSettingsProvider *provider,
                                           int                        *indent_width)
{
  EditorPageEditorconfig *self = EDITOR_PAGE_EDITORCONFIG (provider);
  *indent_width = self->indent_width;
  return self->indent_width_set;
}

static gboolean
editor_page_editorconfig_get_implicit_trailing_newline (EditorPageSettingsProvider *provider,
                                                        gboolean                   *implicit_trailing_newline)
{
  EditorPageEditorconfig *self = EDITOR_PAGE_EDITORCONFIG (provider);
  *implicit_trailing_newline = self->implicit_trailing_newline;
  return self->implicit_trailing_newline_set;
}

static gboolean
editor_page_editorconfig_get_insert_spaces_instead_of_tabs (EditorPageSettingsProvider *provider,
                                                            gboolean                   *insert_spaces_instead_of_tabs)
{
  EditorPageEditorconfig *self = EDITOR_PAGE_EDITORCONFIG (provider);
  *insert_spaces_instead_of_tabs = self->insert_spaces_instead_of_tabs;
  return self->insert_spaces_instead_of_tabs_set;
}

static void
page_settings_provider_iface_init (EditorPageSettingsProviderInterface *iface)
{
  iface->set_document = editor_page_editorconfig_set_document;
  iface->get_insert_spaces_instead_of_tabs = editor_page_editorconfig_get_insert_spaces_instead_of_tabs;
  iface->get_tab_width = editor_page_editorconfig_get_tab_width;
  iface->get_right_margin_position = editor_page_editorconfig_get_right_margin_position;
  iface->get_indent_width = editor_page_editorconfig_get_indent_width;
  iface->get_implicit_trailing_newline = editor_page_editorconfig_get_implicit_trailing_newline;
}

G_DEFINE_TYPE_WITH_CODE (EditorPageEditorconfig, editor_page_editorconfig, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (EDITOR_TYPE_PAGE_SETTINGS_PROVIDER,
                                                page_settings_provider_iface_init))

/**
 * _editor_page_editorconfig_new:
 *
 * Create a new #EditorPageEditorconfig.
 *
 * Returns: (transfer full): a newly created #EditorPageEditorconfig
 */
EditorPageSettingsProvider *
_editor_page_editorconfig_new (void)
{
  return g_object_new (EDITOR_TYPE_PAGE_EDITORCONFIG, NULL);
}

static void
editor_page_editorconfig_dispose (GObject *object)
{
  EditorPageEditorconfig *self = (EditorPageEditorconfig *)object;

  g_clear_weak_pointer (&self->document);

  G_OBJECT_CLASS (editor_page_editorconfig_parent_class)->dispose (object);
}

static void
editor_page_editorconfig_class_init (EditorPageEditorconfigClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_page_editorconfig_dispose;
}

static void
editor_page_editorconfig_init (EditorPageEditorconfig *self)
{
  self->tab_width = 8;
  self->indent_width = -1;
  self->insert_spaces_instead_of_tabs = TRUE;
  self->right_margin_position = 80;
}
