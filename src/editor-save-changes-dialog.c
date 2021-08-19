/* editor-save-changes-dialog.c
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

#define G_LOG_DOMAIN "editor-save-changes-dialog.h"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-document-private.h"
#include "editor-page-private.h"
#include "editor-path-private.h"
#include "editor-save-changes-dialog-private.h"
#include "editor-session-private.h"

typedef struct
{
  /* The page containing the document */
  EditorPage *page;

  /* The document in question */
  EditorDocument *document;

  /* An alternate file to save to (for drafts) */
  GFile *file;

  /* The check button in the dialog */
  GtkCheckButton *check;

  /* Duplicated backpointer because lazy */
  GtkMessageDialog *dialog;
} SaveRequest;

static void
save_request_clear (gpointer data)
{
  SaveRequest *sr = data;

  g_clear_object (&sr->file);
  g_clear_object (&sr->document);
  g_clear_object (&sr->page);
  g_clear_object (&sr->dialog);
}

static void
editor_save_changes_dialog_remove (GArray *requests,
                                   guint   index)
{
  g_autoptr(GtkMessageDialog) dialog = NULL;

  g_assert (requests != NULL);
  g_assert (index < requests->len);

  if (requests->len == 1)
    dialog = g_steal_pointer (&g_array_index (requests, SaveRequest, 0).dialog);

  g_array_remove_index_fast (requests, index);

  if (requests->len == 0)
    {
      GTask *task;

      g_assert (dialog != NULL);

      task = g_object_get_data (G_OBJECT (dialog), "TASK");
      g_task_return_boolean (task, TRUE);
      gtk_window_destroy (GTK_WINDOW (dialog));
    }
}

static void
editor_save_changes_dialog_discard_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  EditorPage *page = (EditorPage *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GArray) requests = user_data;

  g_assert (EDITOR_IS_PAGE (page));
  g_assert (requests != NULL);
  g_assert (requests->len > 0);

  if (!_editor_page_discard_changes_finish (page, result, &error))
    {
      g_autofree gchar *title = editor_page_dup_title (page);
      g_warning ("Failed to discard changes from %s: %s",
                 title, error->message);
    }

  for (guint i = 0; i < requests->len; i++)
    {
      const SaveRequest *sr = &g_array_index (requests, SaveRequest, i);

      if (sr->page == page)
        {
          editor_save_changes_dialog_remove (requests, i);
          break;
        }
    }
}

static void
editor_save_changes_dialog_discard (GtkMessageDialog *dialog,
                                    GArray           *requests)
{
  g_assert (GTK_IS_MESSAGE_DIALOG (dialog));
  g_assert (requests != NULL);
  g_assert (requests->len > 0);

  gtk_widget_set_sensitive (GTK_WIDGET (dialog), FALSE);

  for (guint i = 0; i < requests->len; i++)
    {
      const SaveRequest *sr = &g_array_index (requests, SaveRequest , i);

      _editor_page_discard_changes_async (sr->page,
                                          NULL,
                                          editor_save_changes_dialog_discard_cb,
                                          g_array_ref (requests));
    }
}

static void
editor_save_changes_dialog_save_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  EditorDocument *document = (EditorDocument *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GArray) requests = user_data;

  g_assert (EDITOR_IS_DOCUMENT (document));
  g_assert (requests != NULL);
  g_assert (requests->len > 0);

  if (!_editor_document_save_finish (document, result, &error))
    {
      /* TODO: If we can't save, it should at least persist
       * a draft for us lower in the stack.
       */
      g_autofree gchar *title = editor_document_dup_title (document);
      g_warning ("Failed to save changes from %s: %s",
                 title, error->message);
    }

  for (guint i = 0; i < requests->len; i++)
    {
      const SaveRequest *sr = &g_array_index (requests, SaveRequest, i);

      if (sr->document == document)
        {
          if (error != NULL)
            editor_save_changes_dialog_remove (requests, i);
          else
            _editor_page_discard_changes_async (sr->page,
                                                NULL,
                                                editor_save_changes_dialog_discard_cb,
                                                g_array_ref (requests));
          break;
        }
    }
}

static void
editor_save_changes_dialog_save (GtkMessageDialog *dialog,
                                 GArray           *requests)
{
  g_assert (GTK_IS_MESSAGE_DIALOG (dialog));
  g_assert (requests != NULL);
  g_assert (requests->len > 0);

  gtk_widget_set_sensitive (GTK_WIDGET (dialog), FALSE);

  for (guint i = requests->len; i > 0; i--)
    {
      const SaveRequest *sr = &g_array_index (requests, SaveRequest , i-1);

      if (!gtk_check_button_get_active (sr->check))
        {
          editor_save_changes_dialog_remove (requests, i-1);
          continue;
        }

      _editor_document_save_async (sr->document,
                                   sr->file,
                                   NULL,
                                   editor_save_changes_dialog_save_cb,
                                   g_array_ref (requests));
    }
}

static void
editor_save_changes_dialog_response (GtkMessageDialog *dialog,
                                     int               response,
                                     GArray           *requests)
{
  g_assert (GTK_IS_MESSAGE_DIALOG (dialog));

  if (response == GTK_RESPONSE_NO)
    {
      editor_save_changes_dialog_discard (dialog, requests);
    }
  else if (response == GTK_RESPONSE_YES)
    {
      editor_save_changes_dialog_save (dialog, requests);
    }
  else
    {
      GTask *task = g_object_get_data (G_OBJECT (dialog), "TASK");
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_CANCELLED,
                               "The user cancelled the request");
      gtk_window_destroy (GTK_WINDOW (dialog));
    }
}

static GtkWidget *
_editor_save_changes_dialog_new (GtkWindow *parent,
                                 GPtrArray *pages)
{
  g_autoptr(GArray) requests = NULL;
  const char *discard_label;
  PangoAttrList *smaller;
  GtkWidget *dialog;
  GtkWidget *group;
  GtkWidget *area;

  g_return_val_if_fail (!parent || GTK_IS_WINDOW (parent), NULL);
  g_return_val_if_fail (pages != NULL, NULL);
  g_return_val_if_fail (pages->len > 0, NULL);
  g_return_val_if_fail (EDITOR_IS_PAGE (g_ptr_array_index (pages, 0)), NULL);

  requests = g_array_new (FALSE, FALSE, sizeof (SaveRequest));
  g_array_set_clear_func (requests, save_request_clear);

  discard_label = g_dngettext (GETTEXT_PACKAGE, _("Discard"), _("Discard All"), pages->len);

  dialog = gtk_message_dialog_new (parent,
                                   GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   _("Save Changes?"));
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("Open documents contain unsaved changes. Changes which are not saved will be permanently lost."));
  gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                          _("_Cancel"), GTK_RESPONSE_CANCEL,
                          discard_label, GTK_RESPONSE_NO,
                          _("_Save"), GTK_RESPONSE_YES,
                          NULL);

  area = gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog));
  group = adw_preferences_group_new ();
  gtk_box_append (GTK_BOX (area), group);

  smaller = pango_attr_list_new ();
  pango_attr_list_insert (smaller, pango_attr_scale_new (0.8333));

  for (guint i = 0; i < pages->len; i++)
    {
      EditorPage *page = g_ptr_array_index (pages, i);
      EditorDocument *document = editor_page_get_document (page);
      g_autofree gchar *title_str = editor_document_dup_title (document);
      g_autofree gchar *subtitle_str = NULL;
      GFile *file = editor_document_get_file (document);
      g_autoptr(GFile) directory = file ? g_file_get_parent (file) : NULL;
      GtkWidget *box;
      GtkWidget *check;
      GtkWidget *row;
      GtkWidget *subtitle;
      GtkWidget *title;
      SaveRequest sr;

      if (file == NULL)
        {
          g_autofree gchar *tmp = g_steal_pointer (&title_str);

          if (tmp == NULL)
            tmp = g_strdup (_("Untitled Document"));

          /* translators: %s is replaced with the title of the file */
          title_str = g_strdup_printf (_("%s (new)"), tmp);

          /* Suggest saving in ~/Documents. If they wanted more
           * precision on saving, they can save it outside of this
           * dialog instead.
           */
          directory = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
        }

      if (directory == NULL)
        subtitle_str = g_strdup ("/");
      else if (!g_file_is_native (directory))
        subtitle_str = g_file_get_uri (directory);
      else
        subtitle_str = _editor_path_collapse (g_file_peek_path (directory));

      row = adw_action_row_new ();
      box = g_object_new (GTK_TYPE_BOX,
                          "orientation", GTK_ORIENTATION_VERTICAL,
                          "spacing", 3,
                          "valign", GTK_ALIGN_CENTER,
                          NULL);
      title = g_object_new (GTK_TYPE_LABEL,
                            "halign", GTK_ALIGN_START,
                            "label", title_str,
                            NULL);
      subtitle = g_object_new (GTK_TYPE_LABEL,
                               "halign", GTK_ALIGN_START,
                               "label", subtitle_str,
                               "attributes", smaller,
                               NULL);
      check = g_object_new (GTK_TYPE_CHECK_BUTTON,
                            "active", TRUE,
                            NULL);

      gtk_box_append (GTK_BOX (box), title);
      gtk_box_append (GTK_BOX (box), subtitle);
      adw_action_row_add_prefix (ADW_ACTION_ROW (row), check);
      adw_action_row_add_suffix (ADW_ACTION_ROW (row), box);
      adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), check);
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);

      sr.document = g_object_ref (document);
      sr.check = GTK_CHECK_BUTTON (check);
      sr.page = g_object_ref (page);
      sr.dialog = g_object_ref (GTK_MESSAGE_DIALOG (dialog));

      /* Use NULL for the default file, otherwise set a file
       * so that we write to it instead of the draft.
       */
      if (file != NULL)
        sr.file = g_file_dup (file);
      else
        sr.file = g_file_get_child (directory, editor_document_dup_title (document));

      g_array_append_val (requests, sr);
    }

  pango_attr_list_unref (smaller);

  g_signal_connect_data (dialog,
                         "response",
                         G_CALLBACK (editor_save_changes_dialog_response),
                         g_steal_pointer (&requests),
                         (GClosureNotify) g_array_unref,
                         0);

  return dialog;
}

void
_editor_save_changes_dialog_run_async (GtkWindow           *parent,
                                       GPtrArray           *pages,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  GtkWidget *dialog;

  g_return_if_fail (!parent || GTK_IS_WINDOW (parent));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  dialog = _editor_save_changes_dialog_new (parent, pages);
  task = g_task_new (dialog, cancellable, callback, user_data);
  g_task_set_source_tag (task, _editor_save_changes_dialog_run_async);

  if (pages == NULL || pages->len == 0)
    {
      g_task_return_boolean (task, TRUE);
      return;
    }

  g_object_set_data_full (G_OBJECT (dialog),
                          "TASK",
                          g_steal_pointer (&task),
                          g_object_unref);
  gtk_window_present (GTK_WINDOW (dialog));
}

gboolean
_editor_save_changes_dialog_run_finish (GAsyncResult  *result,
                                        GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
