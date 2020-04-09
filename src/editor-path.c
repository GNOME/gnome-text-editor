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

#include <string.h>
#include <unistd.h>
#include <wordexp.h>

#include "editor-path-private.h"

gchar *
_editor_path_expand (const gchar *path)
{
  wordexp_t state = { 0 };
  gchar *ret = NULL;
  int r;

  if (path == NULL)
    return NULL;

  r = wordexp (path, &state, WRDE_NOCMD);
  if (r == 0 && state.we_wordc > 0)
    ret = g_strdup (state.we_wordv [0]);
  wordfree (&state);

  if (!g_path_is_absolute (ret))
    {
      g_autofree gchar *freeme = ret;

      ret = g_build_filename (g_get_home_dir (), freeme, NULL);
    }

  return ret;
}

gchar *
_editor_path_collapse (const gchar *path)
{
  g_autofree gchar *expanded = NULL;

  if (path == NULL)
    return NULL;

  expanded = _editor_path_expand (path);

  if (g_str_has_prefix (expanded, g_get_home_dir ()))
    return g_build_filename ("~",
                             expanded + strlen (g_get_home_dir ()),
                             NULL);

  return g_steal_pointer (&expanded);
}
