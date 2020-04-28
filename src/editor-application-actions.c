/* editor-application-actions.c
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

#define G_LOG_DOMAIN "editor-application-actions"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-application-private.h"
#include "editor-preferences-window.h"
#include "editor-session.h"
#include "editor-window.h"

static const gchar *authors[] = {
  "Christian Hergert",
  NULL
};

static const gchar *artists[] = {
  "Allan Day",
  NULL
};

static void
editor_application_actions_preferences_cb (GSimpleAction *action,
                                           GVariant      *param,
                                           gpointer       user_data)
{
  EditorApplication *self = user_data;
  EditorPreferencesWindow *prefs;
  EditorWindow *active = NULL;
  GList *windows;

  g_assert (EDITOR_IS_APPLICATION (self));

  windows = gtk_application_get_windows (GTK_APPLICATION (self));

  for (const GList *iter = windows; iter; iter = iter->next)
    {
      GtkWindow *window = iter->data;

      if (active == NULL && EDITOR_IS_WINDOW (window))
        active = EDITOR_WINDOW (window);

      if (EDITOR_IS_PREFERENCES_WINDOW (window))
        {
          gtk_window_present (window);
          return;
        }
    }

  prefs = editor_preferences_window_new (self);
  gtk_window_set_transient_for (GTK_WINDOW (prefs), GTK_WINDOW (active));
  gtk_window_present (GTK_WINDOW (prefs));
}

static void
editor_application_actions_new_window_cb (GSimpleAction *action,
                                          GVariant      *param,
                                          gpointer       user_data)
{
  EditorApplication *self = user_data;
  EditorSession *session;
  EditorWindow *current;

  g_assert (EDITOR_IS_APPLICATION (self));

  current = editor_application_get_current_window (self);
  session = editor_application_get_session (self);

  if (current != NULL && !editor_window_get_visible_page (current))
    {
      editor_session_add_draft (session, current);
      return;
    }

  editor_session_create_window (session);
}

static void
editor_application_actions_about_cb (GSimpleAction *action,
                                     GVariant      *param,
                                     gpointer       user_data)
{
  EditorApplication *self = user_data;
  g_autofree gchar *program_name = NULL;
  GtkAboutDialog *dialog;

  g_assert (EDITOR_IS_APPLICATION (self));

#ifdef DEVELOPMENT_BUILD
  program_name = g_strdup_printf ("%s (Development)", _("Text Editor"));
#else
  program_name = g_strdup (_("Text Editor"));
#endif

  dialog = GTK_ABOUT_DIALOG (gtk_about_dialog_new ());
  gtk_about_dialog_set_program_name (dialog, program_name);
  gtk_about_dialog_set_logo_icon_name (dialog, PACKAGE_ICON_NAME);
  gtk_about_dialog_set_authors (dialog, authors);
  gtk_about_dialog_set_artists (dialog, artists);
  gtk_about_dialog_set_version (dialog, PACKAGE_VERSION);
  gtk_about_dialog_set_copyright (dialog, "© 2020 Christian Hergert");
  gtk_about_dialog_set_license_type (dialog, GTK_LICENSE_GPL_3_0);
  gtk_about_dialog_set_website (dialog, PACKAGE_WEBSITE);
  gtk_about_dialog_set_website_label (dialog, _(PACKAGE_NAME " Website"));

  gtk_window_present (GTK_WINDOW (dialog));
}

void
_editor_application_actions_init (EditorApplication *self)
{
  static const GActionEntry actions[] = {
    { "new-window", editor_application_actions_new_window_cb },
    { "preferences", editor_application_actions_preferences_cb },
    { "about", editor_application_actions_about_cb },
  };

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   actions,
                                   G_N_ELEMENTS (actions),
                                   self);
}
