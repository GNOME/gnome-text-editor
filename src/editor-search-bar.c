/* editor-search-bar.c
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

#define G_LOG_DOMAIN "editor-search-bar"

#include "config.h"

#include "editor-search-bar-private.h"

struct _EditorSearchBar
{
  GtkBin                   parent_instance;

  GtkSourceSearchContext  *context;
  GtkSourceSearchSettings *settings;

  GtkEntry                *search_entry;
  GtkEntry                *replace_entry;
  GtkButton               *replace_button;
  GtkButton               *replace_all_button;
  GtkCheckButton          *case_button;
  GtkCheckButton          *regex_button;
  GtkCheckButton          *word_button;
};

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_TYPE (EditorSearchBar, editor_search_bar, GTK_TYPE_BIN)

static GParamSpec *properties [N_PROPS];

static gboolean
empty_to_null (GBinding     *binding,
               const GValue *from_value,
               GValue       *to_value,
               gpointer      user_data)
{
  const gchar *str = g_value_get_string (from_value);
  if (!str || !*str)
    g_value_set_string (to_value, NULL);
  else
    g_value_set_string (to_value, str);
  return TRUE;
}

static gboolean
null_to_empty (GBinding     *binding,
               const GValue *from_value,
               GValue       *to_value,
               gpointer      user_data)
{
  const gchar *str = g_value_get_string (from_value);
  if (!str || !*str)
    g_value_set_string (to_value, "");
  else
    g_value_set_string (to_value, str);
  return TRUE;
}

static void
editor_search_bar_finalize (GObject *object)
{
  EditorSearchBar *self = (EditorSearchBar *)object;

  g_clear_object (&self->context);
  g_clear_object (&self->settings);

  G_OBJECT_CLASS (editor_search_bar_parent_class)->finalize (object);
}

static void
editor_search_bar_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EditorSearchBar *self = EDITOR_SEARCH_BAR (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_search_bar_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  EditorSearchBar *self = EDITOR_SEARCH_BAR (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_search_bar_class_init (EditorSearchBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = editor_search_bar_finalize;
  object_class->get_property = editor_search_bar_get_property;
  object_class->set_property = editor_search_bar_set_property;

  gtk_widget_class_set_css_name (widget_class, "searchbar");
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-search-bar.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, replace_all_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, replace_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, replace_entry);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, search_entry);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, case_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, word_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, regex_button);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Escape, 0, "search.hide", NULL);
}

static void
editor_search_bar_init (EditorSearchBar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = gtk_source_search_settings_new ();

  gtk_source_search_settings_set_wrap_around (self->settings, TRUE);

  g_object_bind_property_full (self->settings, "search-text",
                               self->search_entry, "text",
                               G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL,
                               null_to_empty, empty_to_null, NULL, NULL);
  g_object_bind_property (self->settings, "at-word-boundaries",
                          self->word_button, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (self->settings, "regex-enabled",
                          self->regex_button, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (self->settings, "case-sensitive",
                          self->case_button, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
}

void
_editor_search_bar_set_mode (EditorSearchBar     *self,
                             EditorSearchBarMode  mode)
{
  gboolean is_replace;

  g_return_if_fail (EDITOR_IS_SEARCH_BAR (self));

  is_replace = mode == EDITOR_SEARCH_BAR_MODE_REPLACE;

  gtk_widget_set_visible (GTK_WIDGET (self->replace_entry), is_replace);
  gtk_widget_set_visible (GTK_WIDGET (self->replace_button), is_replace);
  gtk_widget_set_visible (GTK_WIDGET (self->replace_all_button), is_replace);
}

void
_editor_search_bar_attach (EditorSearchBar *self,
                           EditorDocument  *document)
{
  g_return_if_fail (EDITOR_IS_SEARCH_BAR (self));

  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");
  gtk_editable_set_text (GTK_EDITABLE (self->replace_entry), "");

  if (self->context != NULL)
    return;

  self->context = gtk_source_search_context_new (GTK_SOURCE_BUFFER (document),
                                                 self->settings);
}

void
_editor_search_bar_detach (EditorSearchBar *self)
{
  g_return_if_fail (EDITOR_IS_SEARCH_BAR (self));

  g_clear_object (&self->context);
}
