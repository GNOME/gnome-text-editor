/*
 * editor-encoding-dialog.c
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

#include <glib/gi18n.h>

#include "editor-document-private.h"
#include "editor-encoding-dialog.h"
#include "editor-encoding-model.h"
#include "editor-encoding-row-private.h"
#include "editor-page-private.h"
#include "editor-window.h"

struct _EditorEncodingDialog
{
  AdwDialog            parent_instance;

  EditorDocument      *document;
  EditorPage          *page;
  GPtrArray           *rows;

  AdwPreferencesGroup *group;
  GtkListBox          *list_box;
  GtkSearchEntry      *search_entry;
  GtkStack            *stack;
};

enum {
  PROP_0,
  PROP_PAGE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorEncodingDialog, editor_encoding_dialog, ADW_TYPE_DIALOG)

static GParamSpec *properties[N_PROPS];

static void
editor_encoding_dialog_filter (EditorEncodingDialog *self,
                               const gchar          *text)
{
  g_autoptr(GPatternSpec) spec = NULL;
  GtkWidget *child;
  gboolean had_match = FALSE;

  g_assert (EDITOR_IS_ENCODING_DIALOG (self));

  if (text != NULL && text[0] != 0)
    {
      g_autofree gchar *down = g_utf8_strdown (text, -1);
      g_autofree gchar *glob = g_strdelimit (g_strdup_printf ("*%s*", down), " ", '*');

      spec = g_pattern_spec_new (glob);
    }

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self->list_box));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      if (EDITOR_IS_ENCODING_ROW (child))
        {
          EditorEncodingRow *row = EDITOR_ENCODING_ROW (child);
          gboolean matches = _editor_encoding_row_match (row, spec);

          gtk_widget_set_visible (GTK_WIDGET (row), matches);

          had_match |= matches;
        }
    }

  gtk_widget_set_visible (GTK_WIDGET (self->group), had_match);

  if (!had_match)
    gtk_stack_set_visible_child_name (self->stack, "empty");
  else
    gtk_stack_set_visible_child_name (self->stack, "encodings");
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
editor_encoding_dialog_entry_activate_cb (EditorEncodingDialog *self,
                                          GtkEntry             *entry)
{
  GtkListBoxRow *row;

  g_assert (EDITOR_IS_ENCODING_DIALOG (self));
  g_assert (GTK_IS_ENTRY (entry));

  if ((row = get_first_visible_row (self->list_box)))
    gtk_widget_activate (GTK_WIDGET (row));
}

static void
editor_encoding_dialog_entry_changed_cb (EditorEncodingDialog *self,
                                         GtkSearchEntry       *entry)
{
  const char *text;

  g_assert (EDITOR_IS_ENCODING_DIALOG (self));
  g_assert (GTK_IS_SEARCH_ENTRY (entry));

  text = gtk_editable_get_text (GTK_EDITABLE (entry));
  editor_encoding_dialog_filter (self, text);
}

static void
editor_encoding_dialog_search_stop_cb (EditorEncodingDialog *self)
{
  adw_dialog_close (ADW_DIALOG (self));
}

static void
notify_encoding_cb (EditorEncodingDialog *self,
                    GParamSpec           *pspec,
                    EditorDocument       *document)
{
  g_autoptr(EditorEncoding) encoding = NULL;

  g_assert (EDITOR_IS_ENCODING_DIALOG (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  encoding = editor_document_dup_encoding (document);

  for (guint i = 0; i < self->rows->len; i++)
    {
      EditorEncodingRow *row = g_ptr_array_index (self->rows, i);

      if (encoding == _editor_encoding_row_get_encoding (row))
        _editor_encoding_row_set_selected (row, TRUE);
      else
        _editor_encoding_row_set_selected (row, FALSE);
    }
}

static void
editor_encoding_dialog_reload (EditorEncodingDialog *self,
                               EditorEncoding       *encoding)
{
  g_assert (EDITOR_IS_ENCODING_DIALOG (self));
  g_assert (EDITOR_IS_ENCODING (encoding));

  if (_editor_page_get_window (self->page) == NULL ||
      editor_document_get_busy (self->document))
    return;

  editor_document_set_encoding (self->document, encoding);

  if (editor_document_get_file (self->document) != NULL)
    _editor_page_discard_changes (self->page);

  adw_dialog_close (ADW_DIALOG (self));
}

static void
editor_encoding_dialog_reload_response_cb (EditorEncodingDialog *self,
                                           const char           *response,
                                           AdwAlertDialog       *dialog)
{
  g_assert (EDITOR_IS_ENCODING_DIALOG (self));
  g_assert (ADW_IS_ALERT_DIALOG (dialog));

  if (g_str_equal (response, "reload"))
    editor_encoding_dialog_reload (self, g_object_get_data (G_OBJECT (dialog), "encoding"));
}

static void
editor_encoding_dialog_set_encoding (GtkWidget  *widget,
                                     const char *action,
                                     GVariant   *param)
{
  EditorEncodingDialog *self = EDITOR_ENCODING_DIALOG (widget);
  g_autoptr(EditorEncodingModel) model = NULL;
  g_autoptr(EditorEncoding) encoding = NULL;
  const char *charset;
  guint position;

  g_assert (EDITOR_IS_ENCODING_DIALOG (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_STRING));

  if (editor_document_get_busy (self->document))
    return;

  charset = g_variant_get_string (param, NULL);
  model = editor_encoding_model_new ();
  position = editor_encoding_model_lookup_charset (NULL, charset);
  encoding = g_list_model_get_item (G_LIST_MODEL (model), position);

  if (encoding == NULL)
    return;

  if (editor_document_get_file (self->document) != NULL &&
      gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (self->document)))
    {
      AdwDialog *dialog;

      dialog = adw_alert_dialog_new (_("Reload with a Different Encoding?"),
                                     _("Unsaved changes will be permanently lost."));
      adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                      "cancel", _("_Cancel"),
                                      "reload", _("_Discard Changes and Reload"),
                                      NULL);
      adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");
      adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
      adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                                "reload",
                                                ADW_RESPONSE_DESTRUCTIVE);
      g_object_set_data_full (G_OBJECT (dialog),
                              "encoding",
                              g_object_ref (encoding),
                              g_object_unref);
      g_signal_connect_object (dialog,
                               "response",
                               G_CALLBACK (editor_encoding_dialog_reload_response_cb),
                               self,
                               G_CONNECT_SWAPPED);
      adw_dialog_present (dialog, GTK_WIDGET (_editor_page_get_window (self->page)));
    }
  else
    {
      editor_encoding_dialog_reload (self, encoding);
    }
}

static void
editor_encoding_dialog_constructed (GObject *object)
{
  EditorEncodingDialog *self = (EditorEncodingDialog *)object;
  g_autoptr(EditorEncodingModel) model = editor_encoding_model_new ();
  g_autoptr(EditorEncoding) current = NULL;
  g_autoptr(GError) error = NULL;
  guint n_items = g_list_model_get_n_items (G_LIST_MODEL (model));

  G_OBJECT_CLASS (editor_encoding_dialog_parent_class)->constructed (object);

  current = editor_document_dup_encoding (self->document);
  g_object_get (self->document,
                "error", &error,
                NULL);

  if (error != NULL)
    adw_preferences_group_set_description (self->group, error->message);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(EditorEncoding) encoding = g_list_model_get_item (G_LIST_MODEL (model), i);
      g_autofree char *title = editor_encoding_dup_name (encoding);
      g_autofree char *charset = editor_encoding_dup_charset (encoding);
      GtkWidget *row;

      row = _editor_encoding_row_new (encoding);

      gtk_actionable_set_action_name (GTK_ACTIONABLE (row), "document.encoding");
      gtk_actionable_set_action_target (GTK_ACTIONABLE (row), "s", charset);

      if (current == encoding)
        _editor_encoding_row_set_selected (EDITOR_ENCODING_ROW (row), TRUE);

      gtk_list_box_append (self->list_box, row);

      g_ptr_array_add (self->rows, row);
    }

  g_signal_connect_object (self->document,
                           "notify::encoding",
                           G_CALLBACK (notify_encoding_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

static void
editor_encoding_dialog_dispose (GObject *object)
{
  EditorEncodingDialog *self = (EditorEncodingDialog *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), EDITOR_TYPE_ENCODING_DIALOG);

  g_clear_object (&self->document);
  g_clear_object (&self->page);
  g_clear_pointer (&self->rows, g_ptr_array_unref);

  G_OBJECT_CLASS (editor_encoding_dialog_parent_class)->dispose (object);
}

static void
editor_encoding_dialog_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  EditorEncodingDialog *self = EDITOR_ENCODING_DIALOG (object);

  switch (prop_id)
    {
    case PROP_PAGE:
      g_value_set_object (value, self->page);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_encoding_dialog_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  EditorEncodingDialog *self = EDITOR_ENCODING_DIALOG (object);

  switch (prop_id)
    {
    case PROP_PAGE:
      self->page = g_value_dup_object (value);
      self->document = g_object_ref (editor_page_get_document (self->page));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_encoding_dialog_class_init (EditorEncodingDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = editor_encoding_dialog_constructed;
  object_class->dispose = editor_encoding_dialog_dispose;
  object_class->get_property = editor_encoding_dialog_get_property;
  object_class->set_property = editor_encoding_dialog_set_property;

  properties[PROP_PAGE] =
    g_param_spec_object ("page", NULL, NULL,
                         EDITOR_TYPE_PAGE,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-encoding-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, EditorEncodingDialog, group);
  gtk_widget_class_bind_template_child (widget_class, EditorEncodingDialog, list_box);
  gtk_widget_class_bind_template_child (widget_class, EditorEncodingDialog, search_entry);
  gtk_widget_class_bind_template_child (widget_class, EditorEncodingDialog, stack);

  gtk_widget_class_install_action (widget_class, "document.encoding", "s", editor_encoding_dialog_set_encoding);
}


static void
editor_encoding_dialog_init (EditorEncodingDialog *self)
{
  self->rows = g_ptr_array_new ();

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (self->search_entry,
                           "changed",
                           G_CALLBACK (editor_encoding_dialog_entry_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->search_entry,
                           "stop-search",
                           G_CALLBACK (editor_encoding_dialog_search_stop_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->search_entry,
                           "activate",
                           G_CALLBACK (editor_encoding_dialog_entry_activate_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

AdwDialog *
editor_encoding_dialog_new (EditorPage *page)
{
  g_return_val_if_fail (EDITOR_IS_PAGE (page), NULL);

  return g_object_new (EDITOR_TYPE_ENCODING_DIALOG,
                       "page", page,
                       NULL);
}
