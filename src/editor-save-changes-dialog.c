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
  AdwAlertDialog *dialog;
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
  g_autoptr(AdwAlertDialog) dialog = NULL;

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
editor_save_changes_dialog_discard (AdwAlertDialog *dialog,
                                    GArray         *requests)
{
  g_assert (ADW_IS_ALERT_DIALOG (dialog));
  g_assert (requests != NULL);
  g_assert (requests->len > 0);

  gtk_widget_set_sensitive (GTK_WIDGET (dialog), FALSE);

  for (guint i = 0; i < requests->len; i++)
    {
      const SaveRequest *sr = &g_array_index (requests, SaveRequest , i);

      _editor_page_discard_changes_async (sr->page,
                                          FALSE,
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
                                                FALSE,
                                                NULL,
                                                editor_save_changes_dialog_discard_cb,
                                                g_array_ref (requests));
          break;
        }
    }
}

static void
editor_save_changes_dialog_save (AdwAlertDialog *dialog,
                                 GArray         *requests)
{
  g_assert (ADW_IS_ALERT_DIALOG (dialog));
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
editor_save_changes_dialog_response (AdwAlertDialog *dialog,
                                     const char     *response,
                                     GArray         *requests)
{
  g_assert (ADW_IS_ALERT_DIALOG (dialog));

  if (!g_strcmp0 (response, "discard"))
    {
      editor_save_changes_dialog_discard (dialog, requests);
    }
  else if (!g_strcmp0 (response, "save"))
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
    }
}

static AdwDialog *
_editor_save_changes_dialog_new (GtkWindow *parent,
                                 GPtrArray *pages)
{
  g_autoptr(GArray) requests = NULL;
  const char *discard_label;
  PangoAttrList *smaller;
  AdwDialog *dialog;
  GtkWidget *group;

  g_return_val_if_fail (!parent || GTK_IS_WINDOW (parent), NULL);
  g_return_val_if_fail (pages != NULL, NULL);
  g_return_val_if_fail (pages->len > 0, NULL);
  g_return_val_if_fail (EDITOR_IS_PAGE (g_ptr_array_index (pages, 0)), NULL);

  requests = g_array_new (FALSE, FALSE, sizeof (SaveRequest));
  g_array_set_clear_func (requests, save_request_clear);

  discard_label = g_dngettext (GETTEXT_PACKAGE, _("_Discard"), _("_Discard All"), pages->len);

  dialog = adw_alert_dialog_new (_("Save Changes?"),
                                 _("Open documents contain unsaved changes. Changes which are not saved will be permanently lost."));

  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel", _("_Cancel"),
                                  "discard", discard_label,
                                  "save", _("_Save"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                            "discard", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                            "save", ADW_RESPONSE_SUGGESTED);

  adw_alert_dialog_set_prefer_wide_layout (ADW_ALERT_DIALOG (dialog), TRUE);

  group = adw_preferences_group_new ();
  adw_alert_dialog_set_extra_child (ADW_ALERT_DIALOG (dialog), group);

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
      GtkWidget *check;
      GtkWidget *row;
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
      check = g_object_new (GTK_TYPE_CHECK_BUTTON,
                            "active", TRUE,
                            "valign", GTK_ALIGN_CENTER,
                            NULL);

      adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title_str);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle_str);
      adw_action_row_add_prefix (ADW_ACTION_ROW (row), check);
      adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), check);
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);

      sr.document = g_object_ref (document);
      sr.check = GTK_CHECK_BUTTON (check);
      sr.page = g_object_ref (page);
      sr.dialog = g_object_ref (ADW_ALERT_DIALOG (dialog));

      /* Use NULL for the default file, otherwise set a file
       * so that we write to it instead of the draft.
       */
      if (file != NULL)
        sr.file = g_file_dup (file);
      else
        sr.file = _editor_document_suggest_file (document, directory);

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
  AdwDialog *dialog;

  g_return_if_fail (!parent || GTK_IS_WINDOW (parent));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  dialog = _editor_save_changes_dialog_new (parent, pages);
  task = g_task_new (parent, cancellable, callback, user_data);
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
  adw_dialog_present (dialog, GTK_WIDGET (parent));
}

gboolean
_editor_save_changes_dialog_run_finish (GAsyncResult  *result,
                                        GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
