/* editor-path.c
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

#define G_LOG_DOMAIN "editor-path"

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <string.h>

#ifdef G_OS_UNIX
# include <unistd.h>
# include <wordexp.h>
#endif

#include "editor-path-private.h"

#define FILE_ATTRIBUTE_HOST_PATH "xattr::document-portal.host-path"

char *
_editor_path_expand (const gchar *path)
{
#ifdef G_OS_UNIX
  wordexp_t state = { 0 };
  char *escaped = NULL;
  char *ret = NULL;
  int r;

  if (path == NULL)
    return NULL;

  escaped = g_shell_quote (path);
  r = wordexp (escaped, &state, WRDE_NOCMD);
  if (r == 0 && state.we_wordc > 0)
    ret = g_strdup (state.we_wordv [0]);
  wordfree (&state);

  if (!g_path_is_absolute (ret))
    {
      g_autofree gchar *freeme = ret;

      ret = g_build_filename (g_get_home_dir (), freeme, NULL);
    }

  g_free (escaped);

  return ret;
#else
  return g_strdup (path);
#endif
}

char *
_editor_path_collapse (const gchar *path)
{
#ifdef G_OS_UNIX
  g_autofree gchar *expanded = NULL;

  if (path == NULL)
    return NULL;

  expanded = _editor_path_expand (path);

  /* Special case $HOME to ~/ instead of ~ */
  if (g_str_equal (expanded, g_get_home_dir ()))
    return g_strdup_printf ("~/");

  if (g_str_has_prefix (expanded, g_get_home_dir ()))
    return g_build_filename ("~",
                             expanded + strlen (g_get_home_dir ()),
                             NULL);

  return g_steal_pointer (&expanded);
#else
  return g_strdup (path);
#endif
}

char *
_editor_get_portal_host_path (GFile *file)
{
  g_autoptr(GFileInfo) info = NULL;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  if ((info = g_file_query_info (file,
                                 FILE_ATTRIBUTE_HOST_PATH,
                                 G_FILE_QUERY_INFO_NONE,
                                 NULL, NULL)))
    {
      const char *host_path;

      if ((host_path = g_file_info_get_attribute_string (info, FILE_ATTRIBUTE_HOST_PATH)))
        {
          g_autofree gchar *fs_path = NULL;
          g_autofree gchar *disp_path = NULL;
          gsize len = strlen (host_path);

          /* Early portal versions added a "\x00" suffix, trim it if present */
          if (len > 4 && g_strcmp0 (&host_path[len-4], "\\x00") == 0)
              fs_path = g_strndup (host_path, len - 4);
          else
              fs_path = g_strdup (host_path);

          disp_path = g_filename_display_name (fs_path);
          return _editor_path_collapse (disp_path);
        }
    }

  return g_strdup (_("Document Portal"));
}
