/* editor-recoloring.c
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

#include "editor-recoloring-private.h"

#define SHARED_CSS \
  "@define-color card_fg_color @window_fg_color;\n" \
  "@define-color headerbar_fg_color @window_fg_color;\n" \
  "@define-color headerbar_border_color @window_fg_color;\n" \
  "@define-color popover_fg_color @window_fg_color;\n" \
  "@define-color dark_fill_bg_color @headerbar_bg_color;\n" \
  "@define-color view_bg_color @card_bg_color;\n" \
  "@define-color view_fg_color @window_fg_color;\n"
#define LIGHT_CSS_SUFFIX \
  "@define-color popover_bg_color mix(@window_bg_color, white, .1);\n" \
  "@define-color card_bg_color alpha(white, .65);\n"
#define DARK_CSS_SUFFIX \
  "@define-color popover_bg_color mix(@window_bg_color, black, .1);\n" \
  "@define-color card_bg_color alpha(black, .65);\n"

enum {
  FOREGROUND,
  BACKGROUND,
};

static gboolean
get_color (GtkSourceStyleScheme *scheme,
           const char           *style_name,
           GdkRGBA              *color,
           int                   kind)
{
  GtkSourceStyle *style;
  g_autofree char *fg = NULL;
  g_autofree char *bg = NULL;
  gboolean fg_set = FALSE;
  gboolean bg_set = FALSE;

  g_assert (GTK_SOURCE_IS_STYLE_SCHEME (scheme));
  g_assert (style_name != NULL);

  if (!(style = gtk_source_style_scheme_get_style (scheme, style_name)))
    return FALSE;

  g_object_get (style,
                "foreground", &fg,
                "foreground-set", &fg_set,
                "background", &bg,
                "background-set", &bg_set,
                NULL);

  if (kind == FOREGROUND && fg_set)
    return gdk_rgba_parse (color, fg);
  else if (kind == BACKGROUND && bg_set)
    return gdk_rgba_parse (color, bg);

  return FALSE;
}

static inline gboolean
get_foreground (GtkSourceStyleScheme *scheme,
                const char           *style_name,
                GdkRGBA              *fg)
{
  return get_color (scheme, style_name, fg, FOREGROUND);
}

static inline gboolean
get_background (GtkSourceStyleScheme *scheme,
                const char           *style_name,
                GdkRGBA              *bg)
{
  return get_color (scheme, style_name, bg, BACKGROUND);
}

static void
define_color (GString       *str,
              const char    *name,
              const GdkRGBA *color)
{
  g_autofree char *color_str = NULL;

  g_assert (str != NULL);
  g_assert (name != NULL);
  g_assert (color != NULL);

  color_str = gdk_rgba_to_string (color);
  g_string_append_printf (str, "@define-color %s %s;\n", name, color_str);
}

static gboolean
scheme_is_dark (GtkSourceStyleScheme *scheme)
{
  const char *id = gtk_source_style_scheme_get_id (scheme);
  const char *variant = gtk_source_style_scheme_get_metadata (scheme, "variant");

  if (strstr (id, "-dark") != NULL)
    return TRUE;

  if (g_strcmp0 (variant, "dark") == 0)
    return TRUE;

  return FALSE;
}

char *
_editor_recoloring_generate_css (GtkSourceStyleScheme *style_scheme)
{
  const char *name;
  GString *str;
  GdkRGBA color;

  g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (style_scheme), NULL);

  name = gtk_source_style_scheme_get_name (style_scheme);

  str = g_string_new (SHARED_CSS);
  g_string_append_printf (str, "/* %s */\n", name);

  if (get_background (style_scheme, "text", &color))
    define_color (str, "window_bg_color", &color);

  if (get_foreground (style_scheme, "text", &color))
    define_color (str, "window_fg_color", &color);

  if (get_background (style_scheme, "current-line", &color))
    define_color (str, "headerbar_bg_color", &color);

  if (get_foreground (style_scheme, "selection", &color))
    define_color (str, "accent_color", &color);

  if (get_background (style_scheme, "selection", &color))
    define_color (str, "accent_bg_color", &color);

  if (scheme_is_dark (style_scheme))
    g_string_append (str, DARK_CSS_SUFFIX);
  else
    g_string_append (str, LIGHT_CSS_SUFFIX);

  return g_string_free (str, FALSE);
}
