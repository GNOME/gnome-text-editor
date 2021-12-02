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
#include "editor-recoloring-private.h"
#include "editor-session-private.h"
#include "editor-utils-private.h"
#include "editor-window.h"

G_DEFINE_TYPE (EditorApplication, editor_application, GTK_TYPE_APPLICATION)

enum {
  PROP_0,
  PROP_STYLE_SCHEME,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

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
style_variant_to_color_scheme (GValue   *value,
                               GVariant *variant,
                               gpointer  user_data)
{
  const char *str = g_variant_get_string (variant, NULL);

  if (g_strcmp0 (str, "follow") == 0)
    g_value_set_enum (value, ADW_COLOR_SCHEME_DEFAULT);
  else if (g_strcmp0 (str, "dark") == 0)
    g_value_set_enum (value, ADW_COLOR_SCHEME_FORCE_DARK);
  else
    g_value_set_enum (value, ADW_COLOR_SCHEME_FORCE_LIGHT);

  return TRUE;
}

static void
update_dark (GtkWindow *window)
{
  AdwStyleManager *style_manager;

  g_assert (GTK_IS_WINDOW (window));

  style_manager = adw_style_manager_get_default ();

  if (adw_style_manager_get_dark (style_manager))
    gtk_widget_add_css_class (GTK_WIDGET (window), "dark");
  else
    gtk_widget_remove_css_class (GTK_WIDGET (window), "dark");
}

static void
update_css (EditorApplication *self)
{
  g_autofree char *css = NULL;

  g_assert (EDITOR_IS_APPLICATION (self));

  if (g_settings_get_boolean (self->settings, "recolor-window"))
    {
      GtkSourceStyleSchemeManager *manager;
      GtkSourceStyleScheme *style_scheme;
      const char *id;

      id = editor_application_get_style_scheme (self);
      manager = gtk_source_style_scheme_manager_get_default ();
      style_scheme = gtk_source_style_scheme_manager_get_scheme (manager, id);
      css = _editor_recoloring_generate_css (style_scheme);
    }

  gtk_css_provider_load_from_data (self->recoloring, css ? css : "", -1);
}

static void
on_style_manager_notify_dark (EditorApplication *self,
                              GParamSpec        *pspec,
                              AdwStyleManager   *style_manager)
{
  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (ADW_IS_STYLE_MANAGER (style_manager));

  for (const GList *iter = gtk_application_get_windows (GTK_APPLICATION (self));
       iter != NULL;
       iter = iter->next)
    {
      GtkWindow *window = iter->data;

      if (EDITOR_IS_WINDOW (window))
        update_dark (window);
    }

  update_css (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STYLE_SCHEME]);
}

static void
on_changed_style_scheme_cb (EditorApplication *self,
                            const char        *key,
                            GSettings         *settings)
{
  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (G_IS_SETTINGS (settings));

  update_css (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STYLE_SCHEME]);
}

static void
editor_application_startup (GApplication *application)
{
  static const gchar *quit_accels[] = { "<Primary>Q", NULL };
  static const gchar *help_accels[] = { "F1", NULL };

  EditorApplication *self = (EditorApplication *)application;
  g_autoptr(GtkCssProvider) css_provider = NULL;
  AdwStyleManager *style_manager;
  GdkDisplay *display;

  g_assert (EDITOR_IS_APPLICATION (self));

  G_APPLICATION_CLASS (editor_application_parent_class)->startup (application);

  adw_init ();

  display = gdk_display_get_default ();
  self->recoloring = gtk_css_provider_new ();
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (self->recoloring),
                                              GTK_STYLE_PROVIDER_PRIORITY_THEME+1);

  gtk_application_set_accels_for_action (GTK_APPLICATION (self), "app.quit", quit_accels);
  gtk_application_set_accels_for_action (GTK_APPLICATION (self), "app.help", help_accels);

  _editor_application_actions_init (self);

  style_manager = adw_style_manager_get_default ();

  g_signal_connect_object (style_manager,
                           "notify::dark",
                           G_CALLBACK (on_style_manager_notify_dark),
                           self,
                           G_CONNECT_SWAPPED);

  g_settings_bind (self->settings, "auto-save-delay",
                   self->session, "auto-save-delay",
                   G_SETTINGS_BIND_GET);
  g_settings_bind_with_mapping (self->settings, "style-variant",
                                style_manager, "color-scheme",
                                G_SETTINGS_BIND_GET,
                                style_variant_to_color_scheme,
                                NULL, NULL, NULL);

  /* Setup CSS overrides */
  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (css_provider, "/org/gnome/TextEditor/css/TextEditor.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  /* Setup any recoloring that needs to be done */
  update_css (self);

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
  g_clear_object (&self->recoloring);

  G_APPLICATION_CLASS (editor_application_parent_class)->shutdown (application);
}

static void
editor_application_window_added (GtkApplication *application,
                                 GtkWindow      *window)
{
  g_assert (EDITOR_IS_APPLICATION (application));
  g_assert (GTK_IS_WINDOW (window));

  if (EDITOR_IS_WINDOW (window))
    {
      update_dark (window);
#if DEVELOPMENT_BUILD
      gtk_widget_add_css_class (GTK_WIDGET (window), "devel");
#endif
    }

  GTK_APPLICATION_CLASS (editor_application_parent_class)->window_added (application, window);
}

static void
editor_application_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  EditorApplication *self = EDITOR_APPLICATION (object);

  switch (prop_id)
    {
    case PROP_STYLE_SCHEME:
      g_value_set_string (value, editor_application_get_style_scheme (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_application_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  EditorApplication *self = EDITOR_APPLICATION (object);

  switch (prop_id)
    {
    case PROP_STYLE_SCHEME:
      editor_application_set_style_scheme (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_application_class_init (EditorApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *application_class = G_APPLICATION_CLASS (klass);
  GtkApplicationClass *gtk_application_class = GTK_APPLICATION_CLASS (klass);

  object_class->constructed = editor_application_constructed;
  object_class->get_property = editor_application_get_property;
  object_class->set_property = editor_application_set_property;

  application_class->activate = editor_application_activate;
  application_class->open = editor_application_open;
  application_class->startup = editor_application_startup;
  application_class->shutdown = editor_application_shutdown;
  application_class->handle_local_options = editor_application_handle_local_options;

  gtk_application_class->window_added = editor_application_window_added;

  properties [PROP_STYLE_SCHEME] =
    g_param_spec_string ("style-scheme",
                         "Style Scheme",
                         "The style scheme for the editor",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static const GOptionEntry entries[] = {
  { "ignore-session", 0, 0, G_OPTION_ARG_NONE, NULL, N_("Do not restore session at startup") },
  { 0 }
};

static void
editor_application_init (EditorApplication *self)
{
  self->settings = g_settings_new ("org.gnome.TextEditor");
  self->session = _editor_session_new ();

  g_signal_connect_object (self->settings,
                           "changed::style-scheme",
                           G_CALLBACK (on_changed_style_scheme_cb),
                           self,
                           G_CONNECT_SWAPPED);

  editor_session_set_auto_save (self->session, TRUE);
  if (!g_settings_get_boolean (self->settings, "restore-session"))
    _editor_session_set_restore_pages (self->session, FALSE);

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

static GtkSourceStyleScheme *
_gtk_source_style_scheme_get_variant (GtkSourceStyleScheme *scheme,
                                      const char           *variant)
{
  GtkSourceStyleSchemeManager *style_scheme_manager;
  GtkSourceStyleScheme *ret;
  g_autoptr(GString) str = NULL;
  g_autofree char *key = NULL;
  const char *mapping;

  g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (scheme), NULL);
  g_return_val_if_fail (g_strcmp0 (variant, "light") == 0 ||
                        g_strcmp0 (variant, "dark") == 0, NULL);

  style_scheme_manager = gtk_source_style_scheme_manager_get_default ();

  /* If the scheme provides "light-variant" or "dark-variant" metadata,
   * we will prefer those if the variant is available.
   */
  key = g_strdup_printf ("%s-variant", variant);
  if ((mapping = gtk_source_style_scheme_get_metadata (scheme, key)))
    {
      if ((ret = gtk_source_style_scheme_manager_get_scheme (style_scheme_manager, mapping)))
        return ret;
    }

  /* Try to find a match by replacing -light/-dark with @variant */
  str = g_string_new (gtk_source_style_scheme_get_id (scheme));

  if (g_str_has_suffix (str->str, "-light"))
    g_string_truncate (str, str->len - strlen ("-light"));
  else if (g_str_has_suffix (str->str, "-dark"))
    g_string_truncate (str, str->len - strlen ("-dark"));

  g_string_append_printf (str, "-%s", variant);

  /* Look for "Foo-variant" directly */
  if ((ret = gtk_source_style_scheme_manager_get_scheme (style_scheme_manager, str->str)))
    return ret;

  /* Look for "Foo" */
  g_string_truncate (str, str->len - strlen (variant) - 1);
  if ((ret = gtk_source_style_scheme_manager_get_scheme (style_scheme_manager, str->str)))
    return ret;

  /* Fallback to what we were provided */
  return ret;
}

const char *
editor_application_get_style_scheme (EditorApplication *self)
{
  GtkSourceStyleSchemeManager *style_scheme_manager;
  GtkSourceStyleScheme *style_scheme;
  AdwStyleManager *style_manager;
  g_autofree char *style_scheme_id = NULL;
  const char *variant;

  g_return_val_if_fail (EDITOR_IS_APPLICATION (self), NULL);

  style_manager = adw_style_manager_get_default ();
  style_scheme_manager = gtk_source_style_scheme_manager_get_default ();
  style_scheme_id = g_settings_get_string (self->settings, "style-scheme");

  /* Fallback to Adwaita if we don't find a match */
  if (gtk_source_style_scheme_manager_get_scheme (style_scheme_manager, style_scheme_id) == NULL)
    {
      g_free (style_scheme_id);
      style_scheme_id = g_strdup ("Adwaita");
    }

  if (adw_style_manager_get_dark (style_manager))
    variant = "dark";
  else
    variant = "light";

  style_scheme = gtk_source_style_scheme_manager_get_scheme (style_scheme_manager, style_scheme_id);
  style_scheme = _gtk_source_style_scheme_get_variant (style_scheme, variant);

  return gtk_source_style_scheme_get_id (style_scheme);
}

void
editor_application_set_style_scheme (EditorApplication *self,
                                     const char        *style_scheme)
{
  g_return_if_fail (EDITOR_IS_APPLICATION (self));

  if (style_scheme == NULL)
    style_scheme = "Adwaita";

  g_object_freeze_notify (G_OBJECT (self));
  g_settings_set_string (self->settings, "style-scheme", style_scheme);
  g_object_thaw_notify (G_OBJECT (self));
}
