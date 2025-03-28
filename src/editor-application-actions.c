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

#include "build-ident.h"
#include "config.h"

#include <libspelling.h>

#include <glib/gi18n.h>

#include "editor-application-private.h"
#include "editor-page.h"
#include "editor-save-changes-dialog-private.h"
#include "editor-session-private.h"
#include "editor-window.h"

static const gchar *developers[] = {
  "Christian Hergert",
  "Christopher Davis",
  "Günther Wagner",
  "Lukáš Tyrychtr",
  "Matthias Clasen",
  "Michael Catanzaro",
  "Nick Richards",
  "scootergrisen",
  "vanadiae",
  NULL
};

static const gchar *designers[] = {
  "Allan Day",
  "Jakub Steiner",
  "Tobias Bernard",
  NULL
};

static const gchar *documenters[] = {
  "Anders Jonsson",
  "Christian Hergert",
  "Günther Wagner",
  NULL
};

static char *
get_system_information (void)
{
  GString *str = g_string_new (NULL);
  GtkSettings *gtk_settings = gtk_settings_get_default ();
  g_autoptr(GSettings) settings = g_settings_new ("org.gnome.TextEditor");
  g_autoptr(GSettingsSchema) schema = NULL;
  g_autofree char *theme_name = NULL;
  const char *desktop_session;
  g_auto(GStrv) keys = NULL;

  g_object_get (gtk_settings, "gtk-theme-name", &theme_name, NULL);
  g_object_get (settings, "settings-schema", &schema, NULL);

  g_string_append_printf (str, "%s (%s)", PACKAGE_NAME, EDITOR_BUILD_IDENTIFIER);
#if DEVELOPMENT_BUILD
  g_string_append_printf (str, " Development");
#endif
  g_string_append (str, "\n");

  g_string_append (str, "\n");
  if (g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS))
    g_string_append (str, "Flatpak: yes\n");
  g_string_append_printf (str,
                          "GLib: %d.%d.%d (%d.%d.%d)\n",
                          glib_major_version,
                          glib_minor_version,
                          glib_micro_version,
                          GLIB_MAJOR_VERSION,
                          GLIB_MINOR_VERSION,
                          GLIB_MICRO_VERSION);
  g_string_append_printf (str,
                          "GTK: %d.%d.%d (%d.%d.%d)\n",
                          gtk_get_major_version (),
                          gtk_get_minor_version (),
                          gtk_get_micro_version (),
                          GTK_MAJOR_VERSION,
                          GTK_MINOR_VERSION,
                          GTK_MICRO_VERSION);
  g_string_append_printf (str,
                          "GtkSourceView: %d.%d.%d (%d.%d.%d)\n",
                          gtk_source_get_major_version (),
                          gtk_source_get_minor_version (),
                          gtk_source_get_micro_version (),
                          GTK_SOURCE_MAJOR_VERSION,
                          GTK_SOURCE_MINOR_VERSION,
                          GTK_SOURCE_MICRO_VERSION);
  g_string_append_printf (str,
                          "Libadwaita: %d.%d.%d (%d.%d.%d)\n",
                          adw_get_major_version (),
                          adw_get_minor_version (),
                          adw_get_micro_version (),
                          ADW_MAJOR_VERSION,
                          ADW_MINOR_VERSION,
                          ADW_MICRO_VERSION);
  g_string_append_printf (str,
                          "Libspelling: (%s)\n",
                          SPELLING_VERSION_S);

  g_string_append (str, "\n");
  g_string_append_printf (str, "gtk-theme-name: %s\n", theme_name);
  g_string_append_printf (str, "GTK_THEME: %s\n", g_getenv ("GTK_THEME") ?: "unset");
  g_string_append_printf (str, "GdkDisplay: %s\n", G_OBJECT_TYPE_NAME (gdk_display_get_default ()));

  if (!(desktop_session = g_getenv ("DESKTOP_SESSION")))
    desktop_session = "missing";
  g_string_append_printf (str, "DESKTOP_SESSION: %s\n", desktop_session);

  g_string_append (str, "\n");
  g_string_append_printf (str, "Documents Directory: %s\n", g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));

  g_string_append (str, "\n");
  keys = g_settings_schema_list_keys (schema);
  for (guint i = 0; keys[i]; i++)
    {
      g_autoptr(GSettingsSchemaKey) key = g_settings_schema_get_key (schema, keys[i]);
      g_autoptr(GVariant) default_value = g_settings_schema_key_get_default_value (key);
      g_autoptr(GVariant) user_value = g_settings_get_user_value (settings, keys[i]);
      g_autoptr(GVariant) value = g_settings_get_value (settings, keys[i]);
      g_autofree char *default_str = g_variant_print (default_value, TRUE);
      g_autofree char *value_str = NULL;

      g_string_append_printf (str, "org.gnome.TextEditor %s = ", keys[i]);

      if (user_value != NULL && !g_variant_equal (default_value, user_value))
        {
          value_str = g_variant_print (user_value, TRUE);
          g_string_append_printf (str, "%s [default=%s]\n", value_str, default_str);
        }
      else
        {
          value_str = g_variant_print (value, TRUE);
          g_string_append_printf (str, "%s\n", value_str);
        }
    }

  return g_string_free (str, FALSE);
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

  if (current == NULL)
    g_application_activate (G_APPLICATION (self));
  else if (current != NULL && !editor_window_get_visible_page (current))
    editor_session_add_draft (session, current);
  else
    editor_session_create_window (session);
}

static void
editor_application_actions_about_cb (GSimpleAction *action,
                                     GVariant      *param,
                                     gpointer       user_data)
{
  EditorApplication *self = user_data;
  g_autofree char *system_information = NULL;
  EditorWindow *window;

  g_assert (EDITOR_IS_APPLICATION (self));

  system_information = get_system_information ();

  window = editor_application_get_current_window (self);

  adw_show_about_dialog (GTK_WIDGET (window),
                         "application-name", _("Text Editor"),
                         "application-icon", PACKAGE_ICON_NAME,
                         "developer-name", _("The GNOME Project"),
#if DEVELOPMENT_BUILD
                         "version", EDITOR_BUILD_IDENTIFIER,
#else
                         "version", PACKAGE_VERSION,
#endif
                         "website", PACKAGE_WEBSITE,
                         "issue-url", PACKAGE_BUGREPORTS,
                         "developers", developers,
                         "designers", designers,
                         "copyright", "© 2020-2025 Christian Hergert, et al.",
                         "documenters", documenters,
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "translator-credits", _("translator-credits"),
                         "debug-info", system_information,
                         NULL);
}

static void
editor_application_actions_help_cb (GSimpleAction *action,
                                    GVariant      *param,
                                    gpointer       user_data)
{
  EditorApplication *self = user_data;
  GdkAppLaunchContext *context = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (EDITOR_IS_APPLICATION (self));

  context = gdk_display_get_app_launch_context (gdk_display_get_default ());
  if (!g_app_info_launch_default_for_uri ("help:gnome-text-editor",
                                          G_APP_LAUNCH_CONTEXT (context),
                                          &error))
    g_warning ("Failed to launch help: %s", error->message);
}

static void
editor_application_actions_quit_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  EditorSession *session = (EditorSession *)object;
  g_autoptr(EditorApplication) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (EDITOR_IS_APPLICATION (self));

  if (!editor_session_save_finish (session, result, &error))
    {
      g_warning ("Failed to save session: %s", error->message);
      return;
    }

  g_application_quit (G_APPLICATION (self));
}

static void
editor_application_actions_confirm_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  g_autoptr(EditorApplication) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (EDITOR_IS_APPLICATION (self));

  if (!_editor_save_changes_dialog_run_finish (result, &error))
    return;

  editor_session_save_async (self->session,
                             TRUE,
                             NULL,
                             editor_application_actions_quit_cb,
                             g_object_ref (self));
}

static void
editor_application_actions_quit (GSimpleAction *action,
                                 GVariant      *param,
                                 gpointer       user_data)
{
  EditorApplication *self = user_data;
  g_autoptr(GPtrArray) unsaved = NULL;

  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (EDITOR_IS_APPLICATION (self));

  unsaved = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < self->session->pages->len; i++)
    {
      EditorPage *page = g_ptr_array_index (self->session->pages, i);

      if (editor_page_get_is_modified (page))
        g_ptr_array_add (unsaved, g_object_ref (page));
    }

  if (unsaved->len > 0)
    {
      _editor_save_changes_dialog_run_async (gtk_application_get_active_window (GTK_APPLICATION (self)),
                                             unsaved,
                                             NULL,
                                             editor_application_actions_confirm_cb,
                                             g_object_ref (self));
      return;
    }

  editor_session_save_async (self->session,
                             TRUE,
                             NULL,
                             editor_application_actions_quit_cb,
                             g_object_ref (self));
}

static void
editor_application_actions_remove_recent_cb (GSimpleAction *action,
                                             GVariant      *param,
                                             gpointer       user_data)
{
  g_autoptr(GFile) file = NULL;
  const char *uri;
  const char *draft_id;

  g_assert (EDITOR_IS_APPLICATION (user_data));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE ("(ss)")));

  g_variant_get (param, "(&s&s)", &uri, &draft_id);

  if (uri[0] != 0)
    file = g_file_new_for_uri (uri);

  if (draft_id[0] == 0)
    draft_id = NULL;

  _editor_session_forget (EDITOR_SESSION_DEFAULT, file, draft_id);
}

static void
editor_application_actions_clear_history (GSimpleAction *action,
                                          GVariant      *param,
                                          gpointer       user_data)
{
  EditorApplication *self = user_data;
  EditorWindow *window;
  AdwDialog *dialog;

  g_assert (EDITOR_IS_APPLICATION (self));

  dialog = adw_alert_dialog_new (_("Clear File History?"),
                                 _("This action cannot be undone"));
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel", _("_Cancel"),
                                  "clear-all", _("_Clear All"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                            "clear-all", ADW_RESPONSE_DESTRUCTIVE);

  g_signal_connect_object (dialog,
                           "response::clear-all",
                           G_CALLBACK (_editor_session_clear_history),
                           self->session,
                           G_CONNECT_SWAPPED);

  window = editor_application_get_current_window (self);
  adw_dialog_present (dialog, GTK_WIDGET (window));
}

void
_editor_application_actions_init (EditorApplication *self)
{
  static const GActionEntry actions[] = {
    { "new-window", editor_application_actions_new_window_cb },
    { "about", editor_application_actions_about_cb },
    { "help", editor_application_actions_help_cb },
    { "quit", editor_application_actions_quit },
    { "remove-recent", editor_application_actions_remove_recent_cb, "(ss)" },
    { "clear-history", editor_application_actions_clear_history },
  };
  g_autoptr(GPropertyAction) style_scheme = NULL;
  GAction *action;

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   actions,
                                   G_N_ELEMENTS (actions),
                                   self);

  style_scheme = g_property_action_new ("style-scheme", self, "style-scheme");
  g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (style_scheme));

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "clear-history");
  g_object_bind_property (self->session, "can-clear-history",
                          action, "enabled",
                          G_BINDING_SYNC_CREATE);
}
