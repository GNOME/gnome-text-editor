/* editor-preferences-dialog.c
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

#include <adwaita.h>
#include <gtksourceview/gtksource.h>

#include "editor-page.h"
#include "editor-preferences-dialog-private.h"
#include "editor-session.h"
#include "editor-window.h"
#include "editor-utils-private.h"

struct _EditorPreferencesDialog
{
  AdwPreferencesWindow  parent_instance;
  GSettings            *settings;
  GtkSwitch            *use_system_font;
  GtkGrid              *scheme_group;
  GtkSourceBuffer      *buffer;
  GtkSourceView        *source_view;
};

G_DEFINE_TYPE (EditorPreferencesDialog, editor_preferences_dialog, ADW_TYPE_PREFERENCES_WINDOW)

/* TODO: How and what should be translated here? */
static const struct {
  const char *id;
  const char *example;
} lang_previews[] = {
  { "markdown",
    "# Markdown\n"
    " 1. Numbered Lists\n"
    " * Unnumbered and [Links](https://gnome.org)\n"
    " * `Preformatted Text`\n"
    " * _Emphasis_ or *Emphasis* **Combined**\n"
    "> Block quotes too!"
  },
  { "c",
    "#include <stdio.h>\n"
    "#define EXIT_SUCCESS 0\n"
    "static const struct { int i; double d; char c; } alias;\n"
    "int main (int argc, char *argv[]) {\n"
    "  printf (\"Hello, World! %d %f\\n\", 1, 2.0);\n"
    "  return EXIT_SUCCESS; /* comment */\n"
    "}"
  },
  { "python3",
    "from gi.repository import GObject, Gtk\n"
    "class MyObject(GObject.Object):\n"
    "    def __init__(self, *args, **kwargs):\n"
    "        super().__init__(*args, **kwargs)\n"
  },
  {
    "js",
    "/* JavaScript */\n"
    "const { GLib, Gtk } = imports.gi;\n"
    "var MyObject = class MyObject extends GLib.Object {\n"
    "    constructor(params = {}) {\n"
    "      super();\n"
    "      this._field = null;\n"
    "    }\n"
    "}"
  },
};

static const char *
get_preview (GtkSourceLanguage *lang)
{
  if (lang != NULL)
    {
      const char *id = gtk_source_language_get_id (lang);

      for (guint i = 0; i < G_N_ELEMENTS (lang_previews); i++)
        {
          if (g_strcmp0 (lang_previews[i].id, id) == 0)
            return lang_previews[i].example;
        }
    }

  return NULL;
}

static void
set_preview_language (EditorPreferencesDialog *self,
                      GtkSourceLanguage       *lang,
                      const char              *text)
{
  if (lang == NULL && text == NULL)
    {
      GtkSourceLanguageManager *manager = gtk_source_language_manager_get_default ();
      lang = gtk_source_language_manager_get_language (manager, lang_previews[0].id);
      text = lang_previews[0].example;
    }

  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (self->buffer), text, -1);
  gtk_source_buffer_set_language (self->buffer, lang);
  gtk_source_buffer_set_highlight_syntax (self->buffer, lang != NULL);
}

static void
guess_preview_language (EditorPreferencesDialog *self)
{
  GtkWindow *transient_for;

  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));

  if ((transient_for = gtk_window_get_transient_for (GTK_WINDOW (self))) &&
      EDITOR_IS_WINDOW (transient_for))
    {
      EditorPage *page = editor_window_get_visible_page (EDITOR_WINDOW (transient_for));
      EditorDocument *document;

      if ((document = editor_page_get_document (page)))
        {
          GtkSourceLanguage *lang = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (document));
          const char *text = get_preview (lang);

          if (text)
            {
              set_preview_language (self, lang, text);
              return;
            }
        }
    }

  set_preview_language (self, NULL, NULL);
}

static void
update_style_scheme_selection (EditorPreferencesDialog *self)
{
  g_autofree char *id = NULL;

  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));

  id = g_settings_get_string (self->settings, "style-scheme");

  for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self->scheme_group));
       child;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkSourceStyleSchemePreview *preview = GTK_SOURCE_STYLE_SCHEME_PREVIEW (child);
      GtkSourceStyleScheme *scheme = gtk_source_style_scheme_preview_get_scheme (preview);
      const char *scheme_id = gtk_source_style_scheme_get_id (scheme);
      gboolean selected = g_strcmp0 (scheme_id, id) == 0;

      gtk_source_style_scheme_preview_set_selected (preview, selected);

      if (selected)
        gtk_source_buffer_set_style_scheme (self->buffer, scheme);
    }
}

static gboolean
scheme_is_dark (const char *id)
{
  return strstr (id, "-dark") != NULL;
}

static void
update_style_schemes (EditorPreferencesDialog *self)
{
  GtkSourceStyleSchemeManager *sm;
  const char * const *scheme_ids;
  gboolean is_dark;

  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));

  is_dark = adw_style_manager_get_dark (adw_style_manager_get_default ());

  /* Populate schemes for preferences */
  sm = gtk_source_style_scheme_manager_get_default ();
  if ((scheme_ids = gtk_source_style_scheme_manager_get_scheme_ids (sm)))
    {
      guint j = 0;

      for (guint i = 0; scheme_ids[i]; i++)
        {
          GtkSourceStyleScheme *scheme;
          GtkWidget *preview;

          if (is_dark != scheme_is_dark (scheme_ids[i]))
            continue;

          scheme = gtk_source_style_scheme_manager_get_scheme (sm, scheme_ids[i]);
          preview = gtk_source_style_scheme_preview_new (scheme);
          gtk_actionable_set_action_name (GTK_ACTIONABLE (preview), "app.style-scheme");
          gtk_actionable_set_action_target (GTK_ACTIONABLE (preview), "s", scheme_ids[i]);
          gtk_widget_set_hexpand (preview, TRUE);
          gtk_grid_attach (self->scheme_group, preview, j % 4, j / 4, 1, 1);

          j++;
        }

      update_style_scheme_selection (self);
    }
}

static gboolean
bind_background_pattern (GValue *value,
                         GVariant *variant,
                         gpointer user_data)
{
  if (g_variant_get_boolean (variant))
    g_value_set_enum (value, GTK_SOURCE_BACKGROUND_PATTERN_TYPE_GRID);
  else
    g_value_set_enum (value, GTK_SOURCE_BACKGROUND_PATTERN_TYPE_NONE);
  return TRUE;
}


static void
editor_preferences_dialog_constructed (GObject *object)
{
  EditorPreferencesDialog *self = (EditorPreferencesDialog *)object;

  G_OBJECT_CLASS (editor_preferences_dialog_parent_class)->constructed (object);

  update_style_schemes (self);
  guess_preview_language (self);
}

static void
editor_preferences_dialog_dispose (GObject *object)
{
  EditorPreferencesDialog *self = (EditorPreferencesDialog *)object;

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (editor_preferences_dialog_parent_class)->dispose (object);
}

static void
editor_preferences_dialog_class_init (EditorPreferencesDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = editor_preferences_dialog_constructed;
  object_class->dispose = editor_preferences_dialog_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-preferences-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, buffer);
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, scheme_group);
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, source_view);
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, use_system_font);
}

static void
editor_preferences_dialog_init (EditorPreferencesDialog *self)
{
  AdwStyleManager *style_manager;

  gtk_widget_init_template (GTK_WIDGET (self));

  g_object_set (self,
                "application", g_application_get_default (),
                NULL);

  self->settings = g_settings_new ("org.gnome.TextEditor");
  g_settings_bind (self->settings, "use-system-font",
                   self->use_system_font, "active",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "highlight-current-line",
                   self->source_view, "highlight-current-line",
                   G_SETTINGS_BIND_GET);
  g_settings_bind_with_mapping (self->settings, "show-grid",
                                self->source_view, "background-pattern",
                                G_SETTINGS_BIND_GET,
                                bind_background_pattern, NULL, NULL, NULL);
  g_signal_connect_object (self->settings,
                           "changed::style-scheme",
                           G_CALLBACK (update_style_scheme_selection),
                           self,
                           G_CONNECT_SWAPPED);

  style_manager = adw_style_manager_get_default ();
  g_signal_connect_object (style_manager,
                           "notify::color-scheme",
                           G_CALLBACK (update_style_scheme_selection),
                           self,
                           G_CONNECT_SWAPPED);
}

GtkWidget *
editor_preferences_dialog_new (EditorWindow *transient_for)
{
  g_return_val_if_fail (EDITOR_IS_WINDOW (transient_for), NULL);

  return g_object_new (EDITOR_TYPE_PREFERENCES_DIALOG,
                       "transient-for", transient_for,
                       NULL);
}
