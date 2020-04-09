/* editor-language-dialog.c
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

#define G_LOG_DOMAIN "editor-language-dialog"

#include "config.h"

#include "editor-application.h"
#include "editor-language-dialog.h"
#include "editor-language-row-private.h"

struct _EditorLanguageDialog
{
  GtkWindow          parent_instance;

  GtkListBox        *list_box;
  GtkEntry          *search_entry;

  EditorLanguageRow *selected;
};

enum {
  PROP_0,
  PROP_LANGUAGE,
  N_PROPS
};

G_DEFINE_TYPE (EditorLanguageDialog, editor_language_dialog, GTK_TYPE_WINDOW)

static GParamSpec *properties [N_PROPS];

static void
editor_language_dialog_unselect (EditorLanguageDialog *self)
{
  g_assert (EDITOR_IS_LANGUAGE_DIALOG (self));

  if (self->selected)
    {
      _editor_language_row_set_selected (self->selected, FALSE);
      self->selected = NULL;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_LANGUAGE]);
    }
}

static void
editor_language_dialog_select (EditorLanguageDialog *self,
                               EditorLanguageRow    *row)
{
  g_assert (EDITOR_IS_LANGUAGE_DIALOG (self));
  g_assert (!row || EDITOR_IS_LANGUAGE_ROW (row));

  if (self->selected == row)
    return;

  editor_language_dialog_unselect (self);

  if (row)
    {
      self->selected = row;
      _editor_language_row_set_selected (self->selected, TRUE);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_LANGUAGE]);
    }
}

static GtkWidget *
editor_language_dialog_create_row_cb (gpointer item,
                                      gpointer user_data)
{
  GtkSourceLanguage *language = item;
  EditorLanguageRow *row;

  g_assert (GTK_SOURCE_IS_LANGUAGE (language));

  row = _editor_language_row_new (language);

  return GTK_WIDGET (row);
}

static void
editor_language_dialog_row_activated_cb (EditorLanguageDialog *self,
                                         EditorLanguageRow    *row,
                                         GtkListBox           *list_box)
{
  g_assert (EDITOR_IS_LANGUAGE_DIALOG (self));
  g_assert (EDITOR_IS_LANGUAGE_ROW (row));
  g_assert (GTK_IS_LIST_BOX (list_box));

  editor_language_dialog_select (self, row);
}

static void
editor_language_dialog_filter_cb (EditorLanguageRow *row,
                                  GPatternSpec      *spec)
{
  g_assert (EDITOR_IS_LANGUAGE_ROW (row));

  if (_editor_language_row_match (row, spec))
    gtk_widget_show (GTK_WIDGET (row));
  else
    gtk_widget_hide (GTK_WIDGET (row));
}

static void
editor_language_dialog_filter (EditorLanguageDialog *self,
                               const gchar          *text)
{
  g_autoptr(GPatternSpec) spec = NULL;

  g_assert (EDITOR_IS_LANGUAGE_DIALOG (self));

  if (text != NULL && text[0] != 0)
    {
      g_autofree gchar *down = g_utf8_strdown (text, -1);
      g_autofree gchar *glob = g_strdelimit (g_strdup_printf ("*%s*", down), " ", '*');

      spec = g_pattern_spec_new (glob);
    }

  gtk_container_foreach (GTK_CONTAINER (self->list_box),
                         (GtkCallback) editor_language_dialog_filter_cb,
                         spec);
}

static void
editor_language_dialog_changed_cb (EditorLanguageDialog *self,
                                   GtkEntry             *entry)
{
  const gchar *text;

  g_assert (EDITOR_IS_LANGUAGE_DIALOG (self));
  g_assert (GTK_IS_ENTRY (entry));

  text = gtk_editable_get_text (GTK_EDITABLE (entry));
  editor_language_dialog_filter (self, text);
}

static void
editor_language_dialog_constructed (GObject *object)
{
  EditorLanguageDialog *self = (EditorLanguageDialog *)object;
  GtkSourceLanguageManager *lm;
  g_autoptr(GListStore) store = NULL;
  const gchar * const *ids;

  g_assert (EDITOR_IS_LANGUAGE_DIALOG (self));

  G_OBJECT_CLASS (editor_language_dialog_parent_class)->constructed (object);

  lm = gtk_source_language_manager_get_default ();
  ids = gtk_source_language_manager_get_language_ids (lm);
  store = g_list_store_new (GTK_SOURCE_TYPE_LANGUAGE);

  for (guint i = 0; ids[i]; i++)
    {
      const gchar *id = ids[i];
      GtkSourceLanguage *language;

      if (strcmp (id, "def") == 0)
        continue;

      language = gtk_source_language_manager_get_language (lm, id);
      g_list_store_append (store, language);
    }

  gtk_list_box_bind_model (self->list_box,
                           G_LIST_MODEL (store),
                           editor_language_dialog_create_row_cb,
                           self,
                           NULL);
}

static void
editor_language_dialog_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  EditorLanguageDialog *self = EDITOR_LANGUAGE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_LANGUAGE:
      g_value_set_object (value, editor_language_dialog_get_language (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_language_dialog_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  EditorLanguageDialog *self = EDITOR_LANGUAGE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_LANGUAGE:
      editor_language_dialog_set_language (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_language_dialog_class_init (EditorLanguageDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = editor_language_dialog_constructed;
  object_class->get_property = editor_language_dialog_get_property;
  object_class->set_property = editor_language_dialog_set_property;

  properties [PROP_LANGUAGE] =
    g_param_spec_object ("language",
                         "Language",
                         "The selected language",
                         GTK_SOURCE_TYPE_LANGUAGE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));
  
  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-language-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorLanguageDialog, list_box);
  gtk_widget_class_bind_template_child (widget_class, EditorLanguageDialog, search_entry);
}

static void
editor_language_dialog_init (EditorLanguageDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_window_set_default_size (GTK_WINDOW (self), 500, 500);

  g_signal_connect_object (self->list_box,
                           "row-activated",
                           G_CALLBACK (editor_language_dialog_row_activated_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->search_entry,
                           "changed",
                           G_CALLBACK (editor_language_dialog_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

/**
 * editor_language_dialog_new:
 * @application: (nullable): an #EditorApplication or %NULL
 *
 * Creates a new #EditorLanguageDialog
 *
 * Returns: (transfer full): an #EditorLanguageDialog
 */
EditorLanguageDialog *
editor_language_dialog_new (EditorApplication *application)
{
  g_return_val_if_fail (!application || EDITOR_IS_APPLICATION (application), NULL);

  return g_object_new (EDITOR_TYPE_LANGUAGE_DIALOG,
                       "application", application,
                       NULL);
}

/**
 * editor_language_dialog_get_language:
 * @self: a #EditorLanguageDialog
 *
 * Gets the currently selected language.
 *
 * Returns: (nullable): a #GtkSourceLanguage or %NULL
 */
GtkSourceLanguage *
editor_language_dialog_get_language (EditorLanguageDialog *self)
{
  g_return_val_if_fail (EDITOR_IS_LANGUAGE_DIALOG (self), NULL);

  if (self->selected)
    return _editor_language_row_get_language (self->selected);
  else
    return NULL;
}

static void
editor_language_dialog_set_language_cb (EditorLanguageRow *row,
                                        GtkSourceLanguage *language)
{
  g_assert (EDITOR_IS_LANGUAGE_ROW (row));
  g_assert (!language || GTK_SOURCE_IS_LANGUAGE (language));

  if (language == _editor_language_row_get_language (row))
    {
      GtkWidget *self;

      self = gtk_widget_get_ancestor (GTK_WIDGET (row), EDITOR_TYPE_LANGUAGE_DIALOG);
      editor_language_dialog_select (EDITOR_LANGUAGE_DIALOG (self), row);
    }
}

void
editor_language_dialog_set_language (EditorLanguageDialog *self,
                                     GtkSourceLanguage    *language)
{
  g_return_if_fail (EDITOR_IS_LANGUAGE_DIALOG (self));
  g_return_if_fail (!language || GTK_SOURCE_IS_LANGUAGE (language));

  gtk_container_foreach (GTK_CONTAINER (self->list_box),
                         (GtkCallback) editor_language_dialog_set_language_cb,
                         language);
}
