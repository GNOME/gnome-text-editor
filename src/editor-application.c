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
#include <unistd.h>

#include "editor-application-private.h"
#include "editor-recoloring-private.h"
#include "editor-session-private.h"
#include "editor-utils-private.h"
#include "editor-window.h"

#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_SETTINGS_INTERFACE "org.freedesktop.portal.Settings"

typedef struct _OpenPosition
{
  GFile *file;
  guint line;
  guint line_offset;
} OpenPosition;

typedef struct _Restore
{
  GPtrArray *files;
  char *hint;
} Restore;

G_DEFINE_TYPE (EditorApplication, editor_application, ADW_TYPE_APPLICATION)

enum {
  PROP_0,
  PROP_SESSION,
  PROP_STYLE_SCHEME,
  PROP_SYSTEM_FONT_NAME,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
restore_free (Restore *restore)
{
  g_clear_pointer (&restore->files, g_ptr_array_unref);
  g_clear_pointer (&restore->hint, g_free);
  g_free (restore);
}

static Restore *
restore_new (GFile      **files,
             guint        n_files,
             const char  *hint)
{
  Restore *restore;

  g_assert (files != NULL || n_files == 0);

  restore = g_new0 (Restore, 1);
  restore->files = g_ptr_array_new_with_free_func (g_object_unref);
  restore->hint = g_strdup (hint);

  for (guint i = 0; i < n_files; i++)
    g_ptr_array_add (restore->files, g_object_ref (files[i]));

  return restore;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Restore, restore_free)

static void
editor_application_restore_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  EditorSession *session = (EditorSession *)object;
  g_autoptr(Restore) restore = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (EDITOR_IS_SESSION (session));
  g_assert (restore != NULL);
  g_assert (restore->files != NULL);

  if (!editor_session_restore_finish (session, result, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Failed to restore session: %s", error->message);

      if (restore->files->len == 0)
        editor_session_create_window (session);
    }

  if (restore->files->len > 0)
    editor_session_open_files (session,
                               (GFile **)(gpointer)restore->files->pdata,
                               restore->files->len,
                               restore->hint);

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

  editor_session_set_auto_save (self->session, TRUE);
  editor_session_restore_async (self->session,
                                NULL,
                                editor_application_restore_cb,
                                restore_new (NULL, 0, NULL));
}

static void
editor_application_open (GApplication  *application,
                         GFile        **files,
                         gint           n_files,
                         const gchar   *hint)
{
  EditorApplication *self = (EditorApplication *)application;

  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (files != NULL || n_files == 0);

  if (_editor_session_did_restore (self->session))
    {
      editor_session_open_files (self->session, files, n_files, hint);
    }
  else
    {
      g_application_hold (application);

      editor_session_set_auto_save (self->session, TRUE);
      editor_session_restore_async (self->session,
                                    NULL,
                                    editor_application_restore_cb,
                                    restore_new (files, n_files, hint));
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

  gtk_css_provider_load_from_string (self->recoloring, css ? css : "");
}

static void
on_style_manager_notify_dark (EditorApplication *self,
                              GParamSpec        *pspec,
                              AdwStyleManager   *style_manager)
{
  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (ADW_IS_STYLE_MANAGER (style_manager));

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
on_portal_settings_changed_cb (EditorApplication *self,
                               const char        *sender_name,
                               const char        *signal_name,
                               GVariant          *parameters,
                               gpointer           user_data)
{
  g_autoptr(GVariant) value = NULL;
  const char *schema_id;
  const char *key;

  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (sender_name != NULL);
  g_assert (signal_name != NULL);

  if (g_strcmp0 (signal_name, "SettingChanged") != 0)
    return;

  g_variant_get (parameters, "(&s&sv)", &schema_id, &key, &value);

  if (g_strcmp0 (schema_id, "org.gnome.desktop.interface") == 0 &&
      g_strcmp0 (key, "monospace-font-name") == 0 &&
      g_strcmp0 (g_variant_get_string (value, NULL), "") != 0)
    {
      g_free (self->system_font_name);
      self->system_font_name = g_strdup (g_variant_get_string (value, NULL));
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SYSTEM_FONT_NAME]);
    }
}

static void
parse_portal_settings (EditorApplication *self,
                       GVariant          *parameters)
{
  GVariantIter *iter = NULL;
  const char *schema_str;
  GVariant *val;

  g_assert (EDITOR_IS_APPLICATION (self));

  if (parameters == NULL)
    return;

  g_variant_get (parameters, "(a{sa{sv}})", &iter);

  while (g_variant_iter_loop (iter, "{s@a{sv}}", &schema_str, &val))
    {
      GVariantIter *iter2 = g_variant_iter_new (val);
      const char *key;
      GVariant *v;

      while (g_variant_iter_loop (iter2, "{sv}", &key, &v))
        {
          if (g_strcmp0 (schema_str, "org.gnome.desktop.interface") == 0 &&
              g_strcmp0 (key, "monospace-font-name") == 0 &&
              g_strcmp0 (g_variant_get_string (v, NULL), "") != 0)
            {
              g_free (self->system_font_name);
              self->system_font_name = g_strdup (g_variant_get_string (v, NULL));
            }
        }

      g_variant_iter_free (iter2);
    }

  g_variant_iter_free (iter);
}

static void
editor_application_startup (GApplication *application)
{
  static const char *patterns[] = { "org.gnome.*", NULL };
  static const char *quit_accels[] = { "<Primary>Q", NULL };
  static const char *help_accels[] = { "F1", NULL };

  EditorApplication *self = (EditorApplication *)application;
  GtkSourceStyleSchemeManager *schemes;
  g_autoptr(GVariant) all = NULL;
  g_autofree char *style_path = NULL;
  AdwStyleManager *style_manager;
  GdkDisplay *display;

  g_assert (EDITOR_IS_APPLICATION (self));

  G_APPLICATION_CLASS (editor_application_parent_class)->startup (application);

  gtk_source_init ();

  display = gdk_display_get_default ();
  self->recoloring = gtk_css_provider_new ();
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (self->recoloring),
                                              GTK_STYLE_PROVIDER_PRIORITY_THEME+1);

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_for_display (display),
                                    "/org/gnome/TextEditor/icons");

  gtk_application_set_accels_for_action (GTK_APPLICATION (self), "app.quit", quit_accels);
  gtk_application_set_accels_for_action (GTK_APPLICATION (self), "app.help", help_accels);

  _editor_application_actions_init (self);

  /* Setup portal to get settings */
  self->portal = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                G_DBUS_PROXY_FLAGS_NONE,
                                                NULL,
                                                PORTAL_BUS_NAME,
                                                PORTAL_OBJECT_PATH,
                                                PORTAL_SETTINGS_INTERFACE,
                                                NULL,
                                                NULL);

  if (self->portal != NULL)
    {
      g_signal_connect_object (self->portal,
                               "g-signal",
                               G_CALLBACK (on_portal_settings_changed_cb),
                               self,
                               G_CONNECT_SWAPPED);
      all = g_dbus_proxy_call_sync (self->portal,
                                    "ReadAll",
                                    g_variant_new ("(^as)", patterns),
                                    G_DBUS_CALL_FLAGS_NONE,
                                    G_MAXINT,
                                    NULL,
                                    NULL);
      parse_portal_settings (self, all);
    }

  schemes = gtk_source_style_scheme_manager_get_default ();
  gtk_source_style_scheme_manager_prepend_search_path (schemes, "resource:///org/gnome/TextEditor/gtksourceview/styles/");
  /* add also the user local style folder which is different from the flatpak style folder */
  style_path = g_build_filename (g_get_home_dir (), ".local", "share", "gtksourceview-5", "styles", NULL);
  gtk_source_style_scheme_manager_prepend_search_path (schemes, style_path);

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

  /* Setup any recoloring that needs to be done */
  update_css (self);

  gtk_window_set_default_icon_name (PACKAGE_ICON_NAME);
}

static gboolean
load_stdin_stream_cb (gpointer user_data)
{
  EditorApplication *self = EDITOR_APPLICATION_DEFAULT;
  EditorSession *session;
  EditorWindow *window;
  GInputStream *stream = user_data;

  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (G_IS_INPUT_STREAM (stream));

  window = editor_application_get_current_window (self);
  session = editor_application_get_session (self);
  editor_session_open_stream (session, window, stream);
  g_application_release (G_APPLICATION (self));

  return G_SOURCE_REMOVE;
}

static void
open_position_free (gpointer data)
{
  OpenPosition *op = data;

  g_clear_object (&op->file);
  g_free (op);
}

static OpenPosition *
open_position_copy (OpenPosition *op)
{
  g_object_ref (op->file);
  return g_memdup2 (op, sizeof *op);
}

static void
set_open_position (EditorApplication *self,
                   OpenPosition       position)
{
  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (G_IS_FILE (position.file));

  g_hash_table_insert (self->open_at_position,
                       position.file,
                       open_position_copy (&position));
}

gboolean
_editor_application_consume_position (EditorApplication *self,
                                      GFile             *file,
                                      guint             *line,
                                      guint             *line_offset)
{
  OpenPosition *op;

  g_return_val_if_fail (EDITOR_IS_APPLICATION (self), FALSE);
  g_return_val_if_fail (self->open_at_position != NULL, FALSE);

  if (g_hash_table_steal_extended (self->open_at_position, file, NULL, (gpointer *)&op))
    {
      *line = op->line;
      *line_offset = op->line_offset;

      open_position_free (op);

      return TRUE;
    }

  return FALSE;
}

static gboolean
parse_line_and_offset (const char *str,
                       guint      *line,
                       guint      *line_offset)
{
  int ret = sscanf (str, "+%u:%u", line, line_offset);

  if (ret == 1)
    line_offset = 0;

  return ret > 0;
}

static int
editor_application_command_line (GApplication            *app,
                                 GApplicationCommandLine *command_line)
{
  EditorApplication *self = (EditorApplication *)app;
  g_auto(GStrv) argv = NULL;
  g_autoptr(GPtrArray) files = NULL;
  g_autoptr(GInputStream) stdin_stream = NULL;
  GVariantDict *options;
  gboolean new_window = FALSE;
  const char *hint = NULL;
  int argc;

  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (G_IS_APPLICATION_COMMAND_LINE (command_line));

  /* If the user specified commandline arguments (files) to open, then we
   * should do that instead of allowing GApplication to do it for us. That
   * way we can provide a "hint" about using a new-window.
   */

  argv = g_application_command_line_get_arguments (command_line, &argc);
  options = g_application_command_line_get_options_dict (command_line);
  files = g_ptr_array_new_with_free_func (g_object_unref);

  for (int i = 1; i < argc; i++)
    {
      const char *filename = argv[i];
      g_autoptr(GFile) file = NULL;

      /* We want to read stdin into temporary file if we get '-' */
      if (g_strcmp0 (filename, "-") == 0)
        {
          if (stdin_stream != NULL)
            g_application_command_line_printerr (command_line,
                                                 "%s\n",
                                                 _("Standard input was requested multiple times. Ignoring request."));
          else if (!(stdin_stream = g_application_command_line_get_stdin (command_line)))
            g_application_command_line_printerr (command_line,
                                                 "%s\n",
                                                 _("Standard input is not supported on this platform. Ignoring request."));
          continue;
        }

      file = g_application_command_line_create_file_for_arg (command_line, filename);

      /* If the next argument is +[line:[column]] we want to jump to
       * the line:column of the file.
       */
      if (i+1 < argc && argv[i+1][0] == '+')
        {
          guint line;
          guint line_offset;

          if (argv[i+1][1] == '\0')
            set_open_position (self, (OpenPosition) { file, 0, 0 }), i++;
          else if (parse_line_and_offset (argv[i+1], &line, &line_offset))
            set_open_position (self, (OpenPosition) { file, line, line_offset }), i++;
        }

      /* Otherwise add the file to the list of files we need to open, taking
       * into account the other directory a remote instance could be running
       * from.
       */
      g_ptr_array_add (files, g_steal_pointer (&file));
    }

  /* Only accept --new-window if this is a remote instance, we already
   * create a window if we're in the same process.
   */
  if (g_application_command_line_get_is_remote (command_line) &&
      g_variant_dict_lookup (options, "new-window", "b", &new_window) &&
      new_window)
    hint = "new-window";

  if (files->len > 0)
    g_application_open (app,
                        (GFile **)(gpointer)files->pdata,
                        files->len,
                        hint);
  else if (g_strcmp0 (hint, "new-window") == 0)
    g_action_group_activate_action (G_ACTION_GROUP (app), "new-window", NULL);
  else
    g_application_activate (app);

  /* We've activated but there is a strong chance that our state has not yet
   * been restored because that requires reading from disk. Give a bit of a
   * delay before we process the intput stream for our initial windows to be
   * created and state restored.
   *
   * This is basically a hack, but to do anything else would require more state
   * tracking in the session manager which is particularly difficult as the
   * stdin could be coming from another process which has been passed to us
   * over D-Bus.
   */
  if (stdin_stream != NULL)
    {
      g_application_hold (G_APPLICATION (self));
      g_timeout_add_full (G_PRIORITY_DEFAULT,
                          500 /* msec */,
                          load_stdin_stream_cb,
                          g_steal_pointer (&stdin_stream),
                          g_object_unref);
    }

  return EXIT_SUCCESS;
}

static gint
editor_application_handle_local_options (GApplication *app,
                                         GVariantDict *options)
{
  EditorApplication *self = (EditorApplication *)app;
  gboolean ignore_session = FALSE;

  g_assert (EDITOR_IS_APPLICATION (self));
  g_assert (options != NULL);

  /* This should only happen in the application launching, not the remote
   * instance as we don't want to affect something already running. Running
   * standalone implies --ignore-session so that we don't stomp on any
   * other session data.
   */
  if (self->standalone ||
      g_variant_dict_lookup (options, "ignore-session", "b", &ignore_session))
    _editor_session_set_restore_pages (self->session, FALSE);

  return G_APPLICATION_CLASS (editor_application_parent_class)->handle_local_options (app, options);
}

static void
editor_application_constructed (GObject *object)
{
  EditorApplication *self = (EditorApplication *)object;
  g_autofree char *description = NULL;

  g_assert (EDITOR_IS_APPLICATION (self));

  G_OBJECT_CLASS (editor_application_parent_class)->constructed (object);

  g_application_set_application_id (G_APPLICATION (self), APP_ID);
  g_application_set_resource_base_path (G_APPLICATION (self), "/org/gnome/TextEditor");

  description = g_strdup_printf ("%s %s", _("Bugs may be reported at:"), PACKAGE_BUGREPORTS);
  g_application_set_option_context_description (G_APPLICATION (self), description);
  g_application_set_option_context_parameter_string (G_APPLICATION (self), _("[FILES…]"));
}

static void
editor_application_shutdown (GApplication *application)
{
  EditorApplication *self = (EditorApplication *)application;

  g_clear_object (&self->session);
  g_clear_object (&self->recoloring);
  g_clear_object (&self->portal);
  g_clear_pointer (&self->system_font_name, g_free);
  g_clear_pointer (&self->open_at_position, g_hash_table_unref);

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

    case PROP_SYSTEM_FONT_NAME:
      g_value_set_string (value, self->system_font_name);
      break;

    case PROP_SESSION:
      g_value_set_object (value, self->session);
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
  application_class->command_line = editor_application_command_line;
  application_class->handle_local_options = editor_application_handle_local_options;

  gtk_application_class->window_added = editor_application_window_added;

  properties [PROP_STYLE_SCHEME] =
    g_param_spec_string ("style-scheme",
                         "Style Scheme",
                         "The style scheme for the editor",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SYSTEM_FONT_NAME] =
    g_param_spec_string ("system-font-name",
                         "System Font Name",
                         "System Font Name",
                         "Monospace 11",
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         EDITOR_TYPE_SESSION,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static const GOptionEntry entries[] = {
  { "ignore-session", 'i', 0, G_OPTION_ARG_NONE, NULL, N_("Do not restore session at startup") },
  { "new-window", 'n', 0, G_OPTION_ARG_NONE, NULL, N_("Open provided files in a new window") },
  { "standalone", 's', 0, G_OPTION_ARG_NONE, NULL, N_("Run a new instance of Text Editor (implies --ignore-session)") },
  { "version", 0, 0, G_OPTION_ARG_NONE, NULL, N_("Print version information and exit") },
  { 0 }
};

static void
editor_application_init (EditorApplication *self)
{
  self->settings = g_settings_new ("org.gnome.TextEditor");
  self->session = _editor_session_new ();
  self->system_font_name = g_strdup ("Monospace 11");
  self->open_at_position = g_hash_table_new_full ((GHashFunc)g_file_hash,
                                                  (GEqualFunc)g_file_equal,
                                                  NULL,
                                                  open_position_free);

  g_signal_connect_object (self->settings,
                           "changed::style-scheme",
                           G_CALLBACK (on_changed_style_scheme_cb),
                           self,
                           G_CONNECT_SWAPPED);

  if (!g_settings_get_boolean (self->settings, "restore-session"))
    _editor_session_set_restore_pages (self->session, FALSE);

  g_application_add_main_option_entries (G_APPLICATION (self), entries);
}

EditorApplication *
_editor_application_new (gboolean standalone)
{
  GApplicationFlags flags = G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_HANDLES_OPEN;
  EditorApplication *self;

  if (standalone)
    flags |= G_APPLICATION_NON_UNIQUE;

  self = g_object_new (EDITOR_TYPE_APPLICATION,
                       "flags", flags,
                       NULL);
  self->standalone = !!standalone;

  return self;
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
  style_scheme = _editor_source_style_scheme_get_variant (style_scheme, variant);

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

PangoFontDescription *
_editor_application_get_system_font (EditorApplication *self)
{
  g_return_val_if_fail (EDITOR_IS_APPLICATION (self), NULL);

  return pango_font_description_from_string (self->system_font_name);
}

static GFile *
get_user_style_file (GFile *file)
{
  static GFile *style_dir;
  g_autofree char *basename = NULL;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  if (style_dir == NULL)
    style_dir = g_file_new_build_filename (g_get_user_data_dir (), "gtksourceview-5", "styles", NULL);

  basename = g_file_get_basename (file);
  return g_file_get_child (style_dir, basename);
}

static void
editor_application_install_schemes_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GFile) dst = NULL;
  GPtrArray *ar;
  GFile *src;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  ar = g_task_get_task_data (task);

  g_assert (ar != NULL);
  g_assert (ar->len > 0);
  g_assert (G_IS_FILE (g_ptr_array_index (ar, ar->len-1)));

  g_ptr_array_remove_index (ar, ar->len-1);

  if (!g_file_copy_finish (file, result, &error))
    g_warning ("Failed to copy file: %s", error->message);

  if (ar->len == 0)
    {
      g_task_return_boolean (task, TRUE);
      return;
    }

  src = g_ptr_array_index (ar, ar->len-1);
  dst = get_user_style_file (src);

  g_file_copy_async (src, dst,
                     G_FILE_COPY_OVERWRITE | G_FILE_COPY_BACKUP,
                     G_PRIORITY_LOW,
                     g_task_get_cancellable (task),
                     NULL, NULL,
                     editor_application_install_schemes_cb,
                     g_object_ref (task));
}

void
editor_application_install_schemes_async (EditorApplication    *self,
                                          GFile               **files,
                                          guint                 n_files,
                                          GCancellable         *cancellable,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data)
{
  g_autoptr(GPtrArray) ar = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;
  g_autoptr(GFile) dst = NULL;
  g_autoptr(GFile) dir = NULL;
  GFile *src;

  g_return_if_fail (EDITOR_IS_APPLICATION (self));
  g_return_if_fail (files != NULL);
  g_return_if_fail (n_files > 0);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  ar = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < n_files; i++)
    g_ptr_array_add (ar, g_object_ref (files[i]));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, editor_application_install_schemes_async);
  g_task_set_task_data (task, g_ptr_array_ref (ar), (GDestroyNotify) g_ptr_array_unref);

  src = g_ptr_array_index (ar, ar->len-1);
  dst = get_user_style_file (src);
  dir = g_file_get_parent (dst);

  if (!g_file_query_exists (dir, NULL) &&
      !g_file_make_directory_with_parents (dir, cancellable, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_file_copy_async (src, dst,
                     G_FILE_COPY_OVERWRITE | G_FILE_COPY_BACKUP,
                     G_PRIORITY_LOW,
                     cancellable,
                     NULL, NULL,
                     editor_application_install_schemes_cb,
                     g_steal_pointer (&task));
}

gboolean
editor_application_install_schemes_finish (EditorApplication  *self,
                                           GAsyncResult       *result,
                                           GError            **error)
{
  g_return_val_if_fail (EDITOR_IS_APPLICATION (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
