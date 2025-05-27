/* main.c
 *
 * Copyright 2020 Christian Hergert
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
 */

#define G_LOG_DOMAIN "main"

#include "config.h"

#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

#ifdef HAVE_ENCHANT
# include <enchant.h>
#endif

#include "build-ident.h"
#include "editor-application-private.h"

static void
check_early_opts (int        *argc,
                  char     ***argv,
                  gboolean   *standalone,
                  gboolean   *exit_after_startup)
{
  g_autoptr(GOptionContext) context = NULL;
  gboolean version = FALSE;
  GOptionEntry entries[] = {
    { "standalone", 's', 0, G_OPTION_ARG_NONE, standalone },
    { "version", 0, 0, G_OPTION_ARG_NONE, &version },
    { "exit-after-startup", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, exit_after_startup },
    { NULL }
  };

  context = g_option_context_new (NULL);
  g_option_context_set_ignore_unknown_options (context, TRUE);
  g_option_context_set_help_enabled (context, FALSE);
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, argc, argv, NULL);

  if (version)
    {
      g_print ("%s %s (%s)\n", PACKAGE_NAME, PACKAGE_VERSION, EDITOR_BUILD_IDENTIFIER);
      g_print ("\n");
      g_print ("            GTK: %d.%d.%d (Compiled against %d.%d.%d)\n",
                  gtk_get_major_version (), gtk_get_minor_version (), gtk_get_micro_version (),
                  GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION);
      g_print ("  GtkSourceView: %d.%d.%d (Compiled against %d.%d.%d)\n",
                  gtk_source_get_major_version (), gtk_source_get_minor_version (), gtk_source_get_micro_version (),
                  GTK_SOURCE_MAJOR_VERSION, GTK_SOURCE_MINOR_VERSION, GTK_SOURCE_MICRO_VERSION);

#ifdef HAVE_ENCHANT
      g_print ("        Enchant: %s\n", enchant_get_version ());
#endif

      g_print ("\n\
Copyright 2020-2025 Christian Hergert, et al.\n\
This is free software; see the source for copying conditions. There is NO\n\
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\
");

      exit (EXIT_SUCCESS);
    }
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(EditorApplication) app = NULL;
  gboolean standalone = FALSE;
  gboolean exit_after_startup = FALSE;
  int ret;

  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  check_early_opts (&argc, &argv, &standalone, &exit_after_startup);

  app = _editor_application_new (standalone);

  if (exit_after_startup)
    g_signal_connect_after (app,
                            "startup",
                            G_CALLBACK (g_application_activate),
                            NULL);

  ret = g_application_run (G_APPLICATION (app), argc, argv);

  gtk_source_finalize ();

  return ret;
}
