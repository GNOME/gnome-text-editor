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

#include "editor-application-private.h"

static gboolean
check_standalone (int    *argc,
                  char ***argv)
{
  g_autoptr(GOptionContext) context = NULL;
  gboolean standalone = FALSE;
  GOptionEntry entries[] = {
    { "standalone", 's', 0, G_OPTION_ARG_NONE, &standalone },
    { NULL }
  };

  context = g_option_context_new (NULL);
  g_option_context_set_ignore_unknown_options (context, TRUE);
  g_option_context_set_help_enabled (context, FALSE);
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, argc, argv, NULL);

  return standalone;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(EditorApplication) app = NULL;
  gboolean standalone;
  int ret;

  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  standalone = check_standalone (&argc, &argv);

  gtk_init ();
  gtk_source_init ();

  app = _editor_application_new (standalone);
  ret = g_application_run (G_APPLICATION (app), argc, argv);

  gtk_source_finalize ();

  return ret;
}
