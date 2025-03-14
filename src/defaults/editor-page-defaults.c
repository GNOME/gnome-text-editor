/* editor-page-defaults.c
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

#define G_LOG_DOMAIN "editor-page-defaults"

#include "config.h"

#include <gtksourceview/gtksource.h>

#include "editor-document.h"
#include "editor-page-defaults-private.h"

struct _EditorPageDefaults
{
  GObject parent_instance;
  EditorDocument *document;
};

typedef struct
{
  const gchar *language_id;
  guint right_margin_position;
  guint tab_width;
  int indent_width;
  guint insert_spaces_instead_of_tabs : 1;
} Defaults;

static GHashTable *by_language;
static const Defaults defaults[] = {
  /* https://www.python.org/dev/peps/pep-0008/ */
  { "python", 79, 4, -1, TRUE },
  { "python3", 79, 4, -1, TRUE },

  /* GNOME/GNU style */
  { "c", 80, 8, 2, TRUE },
  { "chdr", 80, 8, 2, TRUE },

  /* https://llvm.org/docs/CodingStandards.html */
  { "cpp", 80, 8, 2, TRUE },
  { "cpphdr", 80, 8, 2, TRUE },

  /* GNOME JS Style */
  { "js", 80, 4, -1, TRUE },

  /* http://www.mono-project.com/community/contributing/coding-guidelines */
  { "c-sharp", 80, 8, -1, FALSE },

  /* https://google.github.io/styleguide/javaguide.html */
  { "java", 100, 2, -1, TRUE },

  /* https://github.com/rubocop-hq/ruby-style-guide */
  { "ruby", 80, 2, -1, TRUE },

  /* https://github.com/rust-lang/rustfmt */
  { "rust", 100, 4, -1, TRUE },

  /* Others */
  { "css", 80, 2, -1, TRUE },
  { "html", 80, 2, -1, TRUE },
  { "makefile", 80, 8, -1, FALSE },
  { "meson", 80, 2, -1, TRUE },
  { "markdown", 0, 4, -1, TRUE },
  { "vala", 100, 4, -1, FALSE },
  { "xml", 80, 2, -1, TRUE },
};

static const Defaults *
get_defaults (EditorPageSettingsProvider *provider)
{
  EditorDocument *document = EDITOR_PAGE_DEFAULTS (provider)->document;
  GtkSourceLanguage *language;
  const char *language_id;

  if (!(language = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (document))))
    return NULL;

  if (!(language_id = gtk_source_language_get_id (language)))
    return NULL;

  return g_hash_table_lookup (by_language, language_id);
}

static void
editor_page_defaults_set_document (EditorPageSettingsProvider *provider,
                                   EditorDocument             *document)
{
  EditorPageDefaults *self = (EditorPageDefaults *)provider;

  g_assert (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  if (g_set_weak_pointer (&self->document, document))
    {
      g_signal_connect_object (document,
                               "notify::language",
                               G_CALLBACK (editor_page_settings_provider_emit_changed),
                               self,
                               G_CONNECT_SWAPPED);
      editor_page_settings_provider_emit_changed (provider);
    }
}

static gboolean
editor_page_defaults_get_right_margin_position (EditorPageSettingsProvider *provider,
                                                guint                      *right_margin_position)
{
  const Defaults *d = get_defaults (provider);
  if (d)
    *right_margin_position = d->right_margin_position;
  return d && d->right_margin_position > 0;
}

static gboolean
editor_page_defaults_get_tab_width (EditorPageSettingsProvider *provider,
                                    guint                      *tab_width)
{
  const Defaults *d = get_defaults (provider);
  if (d)
    *tab_width = d->tab_width;
  return d != NULL;
}

static gboolean
editor_page_defaults_get_indent_width (EditorPageSettingsProvider *provider,
                                       int                        *indent_width)
{
  const Defaults *d = get_defaults (provider);

  if (d)
    {
      *indent_width = d->indent_width;
      return d->indent_width > 0;
    }

  return FALSE;
}

static gboolean
editor_page_defaults_get_insert_spaces_instead_of_tabs (EditorPageSettingsProvider *provider,
                                                        gboolean                   *insert_spaces_instead_of_tabs)
{
  const Defaults *d = get_defaults (provider);
  if (d)
    *insert_spaces_instead_of_tabs = d->insert_spaces_instead_of_tabs;
  return d != NULL;
}

static gboolean
editor_page_defaults_get_implicit_trailing_newline (EditorPageSettingsProvider *provider,
                                                    gboolean                   *implicit_trailing_newline)
{
  *implicit_trailing_newline = TRUE;
  return TRUE;
}

static void
page_settings_provider_iface_init (EditorPageSettingsProviderInterface *iface)
{
  iface->set_document = editor_page_defaults_set_document;
  iface->get_right_margin_position = editor_page_defaults_get_right_margin_position;
  iface->get_tab_width = editor_page_defaults_get_tab_width;
  iface->get_insert_spaces_instead_of_tabs = editor_page_defaults_get_insert_spaces_instead_of_tabs;
  iface->get_indent_width = editor_page_defaults_get_indent_width;
  iface->get_implicit_trailing_newline = editor_page_defaults_get_implicit_trailing_newline;
}

G_DEFINE_TYPE_WITH_CODE (EditorPageDefaults, editor_page_defaults, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (EDITOR_TYPE_PAGE_SETTINGS_PROVIDER,
                                                page_settings_provider_iface_init))

/**
 * _editor_page_defaults_new:
 *
 * Create a new #EditorPageDefaults.
 *
 * Returns: (transfer full): a newly created #EditorPageDefaults
 */
EditorPageSettingsProvider *
_editor_page_defaults_new (void)
{
  return g_object_new (EDITOR_TYPE_PAGE_DEFAULTS, NULL);
}

static void
editor_page_defaults_finalize (GObject *object)
{
  EditorPageDefaults *self = (EditorPageDefaults *)object;

  g_clear_weak_pointer (&self->document);

  G_OBJECT_CLASS (editor_page_defaults_parent_class)->finalize (object);
}

static void
editor_page_defaults_class_init (EditorPageDefaultsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = editor_page_defaults_finalize;

  by_language = g_hash_table_new (g_str_hash, g_str_equal);
  for (guint i = 0; i < G_N_ELEMENTS (defaults); i++)
    g_hash_table_insert (by_language,
                         (gpointer)defaults[i].language_id,
                         (gpointer)&defaults[i]);
}

static void
editor_page_defaults_init (EditorPageDefaults *self)
{
}
