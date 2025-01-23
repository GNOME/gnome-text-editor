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

#include <glib/gi18n.h>

#include "editor-application.h"
#include "editor-language-dialog.h"
#include "editor-language-row-private.h"
#include "editor-session.h"

struct _EditorLanguageDialog
{
  AdwDialog          parent_instance;

  GtkWidget         *group;
  GtkListBox        *list_box;
  GtkWidget         *recent_group;
  GtkListBox        *recent_list_box;
  GtkSearchEntry    *search_entry;
  GtkStack          *stack;

  EditorLanguageRow *selected;
};

enum {
  PROP_0,
  PROP_LANGUAGE,
  N_PROPS
};

G_DEFINE_TYPE (EditorLanguageDialog, editor_language_dialog, ADW_TYPE_DIALOG)

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
editor_language_dialog_filter (EditorLanguageDialog *self,
                               const gchar          *text)
{
  g_autoptr(GPatternSpec) spec = NULL;
  GtkWidget *child;
  gboolean had_recent = FALSE;
  gboolean had_match = FALSE;

  g_assert (EDITOR_IS_LANGUAGE_DIALOG (self));

  if (text != NULL && text[0] != 0)
    {
      g_autofree gchar *down = g_utf8_strdown (text, -1);
      g_autofree gchar *glob = g_strdelimit (g_strdup_printf ("*%s*", down), " ", '*');

      spec = g_pattern_spec_new (glob);
    }

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self->recent_list_box));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      if (EDITOR_IS_LANGUAGE_ROW (child))
        {
          EditorLanguageRow *row = EDITOR_LANGUAGE_ROW (child);
          gboolean matches = _editor_language_row_match (row, spec);

          gtk_widget_set_visible (GTK_WIDGET (row), matches);

          had_recent |= matches;
        }
    }

  gtk_widget_set_visible (GTK_WIDGET (self->recent_group), had_recent);

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self->list_box));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      if (EDITOR_IS_LANGUAGE_ROW (child))
        {
          EditorLanguageRow *row = EDITOR_LANGUAGE_ROW (child);
          gboolean matches = _editor_language_row_match (row, spec);

          gtk_widget_set_visible (GTK_WIDGET (row), matches);

          had_match |= matches;
        }
    }

  gtk_widget_set_visible (GTK_WIDGET (self->group), had_match);

  if (!had_match && !had_recent)
    gtk_stack_set_visible_child_name (self->stack, "no-search-results-page");
  else
    gtk_stack_set_visible_child_name (self->stack, "document-languages-page");
}

static GtkListBoxRow *
get_first_visible_row (GtkListBox *list_box)
{
  GtkWidget *child;

  g_assert (GTK_IS_LIST_BOX (list_box));

  for (child = gtk_widget_get_first_child (GTK_WIDGET (list_box));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      if (gtk_widget_get_visible (child))
        return GTK_LIST_BOX_ROW (child);
    }

  return NULL;
}

static void
editor_language_dialog_entry_activate_cb (EditorLanguageDialog *self,
                                          GtkEntry             *entry)
{
  GtkListBoxRow *row;

  g_assert (EDITOR_IS_LANGUAGE_DIALOG (self));
  g_assert (GTK_IS_ENTRY (entry));

  if ((row = get_first_visible_row (self->list_box)))
    gtk_widget_activate (GTK_WIDGET (row));
}

static void
editor_language_dialog_entry_changed_cb (EditorLanguageDialog *self,
                                         GtkSearchEntry       *entry)
{
  const gchar *text;

  g_assert (EDITOR_IS_LANGUAGE_DIALOG (self));
  g_assert (GTK_IS_SEARCH_ENTRY (entry));

  text = gtk_editable_get_text (GTK_EDITABLE (entry));
  editor_language_dialog_filter (self, text);
}

static void
editor_language_dialog_search_stop_cb (EditorLanguageDialog *self)
{
  g_assert (EDITOR_IS_LANGUAGE_DIALOG (self));

  adw_dialog_close (ADW_DIALOG (self));
}

static void
editor_language_dialog_constructed (GObject *object)
{
  EditorLanguageDialog *self = (EditorLanguageDialog *)object;
  GtkSourceLanguageManager *lm;
  g_autoptr(GListModel) recent = NULL;
  g_autoptr(GHashTable) seen = NULL;
  const gchar * const *ids;
  guint n_items;

  g_assert (EDITOR_IS_LANGUAGE_DIALOG (self));

  G_OBJECT_CLASS (editor_language_dialog_parent_class)->constructed (object);

  lm = gtk_source_language_manager_get_default ();
  ids = gtk_source_language_manager_get_language_ids (lm);
  recent = editor_session_list_recent_syntaxes (EDITOR_SESSION_DEFAULT);
  n_items = g_list_model_get_n_items (recent);
  seen = g_hash_table_new (NULL, NULL);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(GtkSourceLanguage) language = g_list_model_get_item (recent, i);

      if (!gtk_source_language_get_hidden (language))
        {
          gtk_list_box_append (self->recent_list_box, _editor_language_row_new (language));
          g_hash_table_add (seen, language);
        }
    }

  gtk_widget_set_visible (GTK_WIDGET (self->recent_group),
                          g_hash_table_size (seen) > 0);

  if (g_hash_table_size (seen) > 0)
    adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (self->group),
                                     _("Other Document Types"));
  else
    adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (self->group), NULL);

  gtk_list_box_append (self->list_box, _editor_language_row_new (NULL));

  for (guint i = 0; ids[i]; i++)
    {
      const gchar *id = ids[i];
      GtkSourceLanguage *language = gtk_source_language_manager_get_language (lm, id);

      if (g_hash_table_lookup (seen, language))
        continue;

      if (!gtk_source_language_get_hidden (language))
        gtk_list_box_append (self->list_box, _editor_language_row_new (language));
    }

  gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
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

  gtk_widget_class_bind_template_child (widget_class, EditorLanguageDialog, group);
  gtk_widget_class_bind_template_child (widget_class, EditorLanguageDialog, list_box);
  gtk_widget_class_bind_template_child (widget_class, EditorLanguageDialog, recent_group);
  gtk_widget_class_bind_template_child (widget_class, EditorLanguageDialog, recent_list_box);
  gtk_widget_class_bind_template_child (widget_class, EditorLanguageDialog, search_entry);
  gtk_widget_class_bind_template_child (widget_class, EditorLanguageDialog, stack);
}

static void
editor_language_dialog_init (EditorLanguageDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

#if DEVELOPMENT_BUILD
  gtk_widget_add_css_class (GTK_WIDGET (self), "devel");
#endif

  adw_dialog_set_content_width (ADW_DIALOG (self), 350);

  g_signal_connect_object (self->list_box,
                           "row-activated",
                           G_CALLBACK (editor_language_dialog_row_activated_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->recent_list_box,
                           "row-activated",
                           G_CALLBACK (editor_language_dialog_row_activated_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->search_entry,
                           "activate",
                           G_CALLBACK (editor_language_dialog_entry_activate_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->search_entry,
                           "changed",
                           G_CALLBACK (editor_language_dialog_entry_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->search_entry,
                           "stop-search",
                           G_CALLBACK (editor_language_dialog_search_stop_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

/**
 * editor_language_dialog_new:
 *
 * Creates a new #EditorLanguageDialog
 *
 * Returns: (transfer full): an #EditorLanguageDialog
 */
EditorLanguageDialog *
editor_language_dialog_new (void)
{
  return g_object_new (EDITOR_TYPE_LANGUAGE_DIALOG, NULL);
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

void
editor_language_dialog_set_language (EditorLanguageDialog *self,
                                     GtkSourceLanguage    *language)
{
  GtkWidget *child;

  g_return_if_fail (EDITOR_IS_LANGUAGE_DIALOG (self));
  g_return_if_fail (!language || GTK_SOURCE_IS_LANGUAGE (language));

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self->recent_list_box));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      if (EDITOR_IS_LANGUAGE_ROW (child))
        {
          EditorLanguageRow *row = EDITOR_LANGUAGE_ROW (child);

          if (language == _editor_language_row_get_language (row))
            {
              editor_language_dialog_select (EDITOR_LANGUAGE_DIALOG (self), row);
              return;
            }
        }
    }

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self->list_box));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      if (EDITOR_IS_LANGUAGE_ROW (child))
        {
          EditorLanguageRow *row = EDITOR_LANGUAGE_ROW (child);

          if (language == _editor_language_row_get_language (row))
            {
              editor_language_dialog_select (EDITOR_LANGUAGE_DIALOG (self), row);
              return;
            }
        }
    }
}
