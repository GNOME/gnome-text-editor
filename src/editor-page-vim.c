/* editor-page-vim.c
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

#include "editor-application-private.h"
#include "editor-page-private.h"
#include "editor-session.h"

static void
on_vim_write_cb (EditorPage            *self,
                 GtkSourceView         *view,
                 const char            *path,
                 GtkSourceVimIMContext *im_context)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (GTK_SOURCE_IS_VIEW (view));
  g_assert (GTK_SOURCE_IS_VIM_IM_CONTEXT (im_context));

  if (path != NULL)
    _editor_page_save_as (self, path);
  else
    _editor_page_save (self);
}

static void
on_vim_edit_cb (EditorPage            *self,
                GtkSourceView         *view,
                const char            *path,
                GtkSourceVimIMContext *im_context)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (GTK_SOURCE_IS_VIEW (view));
  g_assert (GTK_SOURCE_IS_VIM_IM_CONTEXT (im_context));

  if (path != NULL)
    {
      GFile *file = editor_document_get_file (self->document);
      g_autoptr(GFile) selected = NULL;

      if (file != NULL && !g_path_is_absolute (path))
        {
          g_autoptr(GFile) parent = g_file_get_parent (file);
          selected = g_file_get_child (parent, path);
        }
      else
        {
          selected = g_file_new_for_path (path);
        }

      editor_session_open (EDITOR_SESSION_DEFAULT,
                           EDITOR_WINDOW (gtk_widget_get_native (GTK_WIDGET (self))),
                           selected,
                           NULL);
    }
  else
    {
      _editor_page_discard_changes (self);
    }
}

static void
close_after_complete_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr(EditorPage) self = user_data;

  g_assert (EDITOR_IS_PAGE (self));

  editor_session_remove_page (EDITOR_SESSION_DEFAULT, self);
}

static gboolean
on_vim_execute_command_cb (EditorPage            *self,
                           const char            *command,
                           GtkSourceVimIMContext *im_context)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (GTK_SOURCE_IS_VIM_IM_CONTEXT (im_context));

  if (g_str_equal (command, ":q") ||
      g_str_equal (command, ":quit") ||
      g_str_equal (command, "^Wc"))
    {
      editor_session_remove_page (EDITOR_SESSION_DEFAULT, self);
      return TRUE;
    }

  if (g_str_equal (command, ":q!") || g_str_equal (command, ":quit!"))
    {
      _editor_page_discard_changes_async (self, FALSE, NULL, close_after_complete_cb, g_object_ref (self));
      return TRUE;
    }

  if (g_str_equal (command, ":wq"))
    {
      _editor_document_save_async (self->document, NULL, NULL, close_after_complete_cb, g_object_ref (self));
      return TRUE;
    }

  return FALSE;
}

static void
on_keybindings_changed_cb (EditorPage *self,
                           const char *key,
                           GSettings  *settings)
{
  g_autofree char *choice = NULL;

  g_assert (EDITOR_IS_PAGE (self));
  g_assert (G_IS_SETTINGS (settings));

  choice = g_settings_get_string (settings, "keybindings");

  if (g_str_equal (choice, "vim"))
    {
      if (self->vim == NULL)
        {
          GtkIMContext *im_context = gtk_source_vim_im_context_new ();

          g_signal_connect_object (im_context,
                                   "write",
                                   G_CALLBACK (on_vim_write_cb),
                                   self,
                                   G_CONNECT_SWAPPED);
          g_signal_connect_object (im_context,
                                   "edit",
                                   G_CALLBACK (on_vim_edit_cb),
                                   self,
                                   G_CONNECT_SWAPPED);
          g_signal_connect_object (im_context,
                                   "execute-command",
                                   G_CALLBACK (on_vim_execute_command_cb),
                                   self,
                                   G_CONNECT_SWAPPED);
          gtk_im_context_set_client_widget (im_context, GTK_WIDGET (self->view));

          self->vim = gtk_event_controller_key_new ();
          gtk_event_controller_set_propagation_phase (self->vim, GTK_PHASE_CAPTURE);
          gtk_event_controller_key_set_im_context (GTK_EVENT_CONTROLLER_KEY (self->vim), im_context);
          gtk_widget_add_controller (GTK_WIDGET (self->view), self->vim);
        }
    }
  else
    {
      if (self->vim)
        {
          gtk_widget_remove_controller (GTK_WIDGET (self->view), self->vim);
          self->vim = NULL;
          gtk_text_view_set_overwrite (GTK_TEXT_VIEW (self->view), FALSE);
        }
    }
}

void
_editor_page_vim_init (EditorPage *self)
{
  EditorApplication *app = EDITOR_APPLICATION_DEFAULT;

  g_return_if_fail (EDITOR_IS_PAGE (self));

  g_signal_connect_object (app->settings,
                           "changed::keybindings",
                           G_CALLBACK (on_keybindings_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  on_keybindings_changed_cb (self, NULL, app->settings);
}
