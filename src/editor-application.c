/* editor-application.c
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

#define G_LOG_DOMAIN "editor-application"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-application-private.h"
#include "editor-session-private.h"
#include "editor-utils-private.h"
#include "editor-window.h"

G_DEFINE_TYPE (EditorApplication, editor_application, GTK_TYPE_APPLICATION)

static void
editor_application_restore_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  EditorSession *session = (EditorSession *)object;
  g_autoptr(GPtrArray) files = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (EDITOR_IS_SESSION (session));

  if (!editor_session_restore_finish (session, result, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Failed to restore session: %s", error->message);

      if (files == NULL || files->len == 0)
        editor_session_create_window (session);
    }

  if (files != NULL && files->len > 0)
    editor_session_open_files (session, (GFile **)files->pdata, files->len);

  g_application_release (g_application_get_default ());
}

static void
editor_application_activate (GApplication *application)
{
  EditorApplication *self = (EditorApplication *)application;
  const GList *windows;

  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (EDITOR_IS_SESSION (self->session));

  windows = gtk_application_get_windows (GTK_APPLICATION (application));

  for (const GList *iter = windows; iter; iter = iter->next)
    {
      GtkWindow *window = iter->data;

      if (EDITOR_IS_WINDOW (window))
        {
          gtk_window_present (window);
          return;
        }
    }

  /* At this point, we had no existing windows to activate. We can assume
   * that we are restoring the previous session (if any) now. It might be
   * possible to start shutdown and race with an activation, but reloading
   * the session is probably fine in either case.
   */

  g_application_hold (application);

  editor_session_restore_async (self->session,
                                NULL,
                                editor_application_restore_cb,
                                NULL);
}

static void
editor_application_open (GApplication  *application,
                         GFile        **files,
                         gint           n_files,
                         const gchar   *hint)
{
  EditorApplication *self = (EditorApplication *)application;
  g_autoptr(GPtrArray) ar = NULL;

  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (files != NULL);
  g_assert (n_files > 0);

  ar = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < n_files; i++)
    g_ptr_array_add (ar, g_file_dup (files[i]));

  /* Restore the session first if there are no windows */
  if (!_editor_session_did_restore (self->session))
    {
      g_application_hold (application);
      editor_session_restore_async (self->session,
                                    NULL,
                                    editor_application_restore_cb,
                                    g_steal_pointer (&ar));
    }
  else if (ar->len > 0)
    {
      editor_session_open_files (self->session,
                                 (GFile **)ar->pdata,
                                 ar->len);
    }
}

static gboolean
style_variant_to_boolean (GValue   *value,
                          GVariant *variant,
                          gpointer  user_data)
{
  g_value_set_boolean (value, g_strcmp0 (g_variant_get_string (variant, NULL), "dark") == 0);
  return TRUE;
}

static void
editor_application_startup (GApplication *application)
{
  EditorApplication *self = (EditorApplication *)application;
  g_autoptr(GtkCssProvider) css_provider = NULL;
  GtkSettings *gtk_settings;
  static const gchar *quit_accels[] = { "<Primary>Q", NULL };
  static const gchar *help_accels[] = { "F1", NULL };

  g_assert (EDITOR_IS_APPLICATION (self));

  G_APPLICATION_CLASS (editor_application_parent_class)->startup (application);

  adw_init ();

  gtk_application_set_accels_for_action (GTK_APPLICATION (self), "app.quit", quit_accels);
  gtk_application_set_accels_for_action (GTK_APPLICATION (self), "app.help", help_accels);

  self->settings = g_settings_new ("org.gnome.TextEditor");

  _editor_application_actions_init (self);

  gtk_settings = gtk_settings_get_default ();

  g_settings_bind (self->settings, "auto-save-delay",
                   self->session, "auto-save-delay",
                   G_SETTINGS_BIND_GET);
  g_settings_bind_with_mapping (self->settings, "style-variant",
                                gtk_settings, "gtk-application-prefer-dark-theme",
                                G_SETTINGS_BIND_GET,
                                style_variant_to_boolean,
                                NULL, NULL, NULL);

  /* Setup CSS overrides */
  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (css_provider, "/org/gnome/TextEditor/css/TextEditor.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_window_set_default_icon_name (PACKAGE_ICON_NAME);
}

static gint
editor_application_handle_local_options (GApplication *app,
                                         GVariantDict *options)
{
  EditorApplication *self = (EditorApplication *)app;
  gboolean ignore_session;

  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (options != NULL);

  if (g_variant_dict_lookup (options, "ignore-session", "b", &ignore_session))
    _editor_session_set_restore_pages (self->session, FALSE);

  return G_APPLICATION_CLASS (editor_application_parent_class)->handle_local_options (app, options);
}

static void
editor_application_constructed (GObject *object)
{
  EditorApplication *self = (EditorApplication *)object;

  g_assert (EDITOR_IS_APPLICATION (self));

  G_OBJECT_CLASS (editor_application_parent_class)->constructed (object);

  g_application_set_application_id (G_APPLICATION (self), APP_ID);
  g_application_set_resource_base_path (G_APPLICATION (self), "/org/gnome/TextEditor");
  g_application_set_flags (G_APPLICATION (self), G_APPLICATION_HANDLES_OPEN);
}

static void
editor_application_shutdown (GApplication *application)
{
  EditorApplication *self = (EditorApplication *)application;

  g_clear_object (&self->session);

  G_APPLICATION_CLASS (editor_application_parent_class)->shutdown (application);
}

static void
editor_application_class_init (EditorApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

  object_class->constructed = editor_application_constructed;

  application_class->activate = editor_application_activate;
  application_class->open = editor_application_open;
  application_class->startup = editor_application_startup;
  application_class->shutdown = editor_application_shutdown;
  application_class->handle_local_options = editor_application_handle_local_options;
}

static const GOptionEntry entries[] = {
  { "ignore-session", 0, 0, G_OPTION_ARG_NONE, NULL, N_("Do not restore session at startup") },
  { 0 }
};

static void
editor_application_init (EditorApplication *self)
{
  self->session = _editor_session_new ();
  editor_session_set_auto_save (self->session, TRUE);

  g_application_add_main_option_entries (G_APPLICATION (self), entries);
}

EditorApplication *
_editor_application_new (void)
{
  return g_object_new (EDITOR_TYPE_APPLICATION, NULL);
}

/**
 * editor_application_get_session:
 * @self: a #EditorApplication
 *
 * Gets the session manager for the application.
 *
 * Returns: (transfer none): an #EditorSession
 */
EditorSession *
editor_application_get_session (EditorApplication *self)
{
  g_return_val_if_fail (EDITOR_IS_APPLICATION (self), NULL);

  return self->session;
}

/**
 * editor_application_get_current_window:
 * @self: a #EditorApplication
 *
 * Gets the current #EditorWindow for the application if there is one.
 *
 * Returns: (transfer none) (nullable): an #EditorWindow or %NULL
 */
EditorWindow *
editor_application_get_current_window (EditorApplication *self)
{
  const GList *windows;

  g_return_val_if_fail (EDITOR_IS_APPLICATION (self), NULL);

  windows = gtk_application_get_windows (GTK_APPLICATION (self));

  for (const GList *iter = windows; iter; iter = iter->next)
    {
      GtkWindow *window = iter->data;

      if (EDITOR_IS_WINDOW (window))
        return EDITOR_WINDOW (window);
    }

  return NULL;
}
