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
  "@define-color card_bg_color alpha(white, .6);\n"
#define DARK_CSS_SUFFIX \
  "@define-color popover_bg_color mix(@window_bg_color, black, .1);\n" \
  "@define-color card_bg_color alpha(white, .1);\n"

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
    gdk_rgba_parse (color, fg);
  else if (kind == BACKGROUND && bg_set)
    gdk_rgba_parse (color, bg);
  else
    return FALSE;

  return color->alpha > 0.0;
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
  GdkRGBA opaque;

  g_assert (str != NULL);
  g_assert (name != NULL);
  g_assert (color != NULL);

  opaque = *color;
  opaque.alpha = 1.0f;

  color_str = gdk_rgba_to_string (&opaque);
  g_string_append_printf (str, "@define-color %s %s;\n", name, color_str);
}

static void
define_color_mixed (GString       *str,
                    const char    *name,
                    const GdkRGBA *a,
                    const GdkRGBA *b,
                    double         level)
{
  g_autofree char *a_str = NULL;
  g_autofree char *b_str = NULL;

  g_assert (str != NULL);
  g_assert (name != NULL);
  g_assert (a != NULL);
  g_assert (b != NULL);

  a_str = gdk_rgba_to_string (a);
  b_str = gdk_rgba_to_string (b);

  g_string_append_printf (str, "@define-color %s mix(%s,%s,%lf);\n", name, a_str, b_str, level);
}

static inline void
premix_colors (GdkRGBA       *dest,
               const GdkRGBA *fg,
               const GdkRGBA *bg,
               gboolean       bg_set,
               double         alpha)
{
  g_assert (dest != NULL);
  g_assert (fg != NULL);
  g_assert (bg != NULL || bg_set == FALSE);
  g_assert (alpha >= 0.0 && alpha <= 1.0);

  if (bg_set)
    {
      dest->red = ((1 - alpha) * bg->red) + (alpha * fg->red);
      dest->green = ((1 - alpha) * bg->green) + (alpha * fg->green);
      dest->blue = ((1 - alpha) * bg->blue) + (alpha * fg->blue);
      dest->alpha = 1.0;
    }
  else
    {
      *dest = *fg;
      dest->alpha = alpha;
    }
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
  static const GdkRGBA black = {0,0,0,1};
  static const GdkRGBA white = {1,1,1,1};
  const GdkRGBA *alt;
  GdkRGBA text_bg;
  GdkRGBA text_fg;
  GdkRGBA right_margin;
  const char *id;
  const char *name;
  GString *str;
  GdkRGBA color;
  gboolean is_dark;

  g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (style_scheme), NULL);

  /* Don't restyle Adwaita as we already have it */
  id = gtk_source_style_scheme_get_name (style_scheme);
  if (g_str_has_prefix (id, "Adwaita"))
    return NULL;

  name = gtk_source_style_scheme_get_name (style_scheme);
  is_dark = scheme_is_dark (style_scheme);
  alt = is_dark ? &white : &black;

  str = g_string_new (SHARED_CSS);
  g_string_append_printf (str, "/* %s */\n", name);

  /* TODO: Improve error checking and fallbacks */

  get_background (style_scheme, "text", &text_bg);
  get_foreground (style_scheme, "text", &text_fg);
  get_background (style_scheme, "right-margin", &right_margin);
  right_margin.alpha = 1;

  premix_colors (&color, &text_bg, &right_margin, TRUE, 1.0);
  if (is_dark)
    define_color_mixed (str, "window_bg_color", &text_bg, alt, .025);
  else
    define_color_mixed (str, "window_bg_color", &text_bg, &white, .1);
  define_color_mixed (str, "window_fg_color", &text_fg, alt, .1);

  premix_colors (&color, &text_bg, &text_fg, TRUE, 1.0);
  if (is_dark)
    define_color_mixed (str, "headerbar_bg_color", &text_bg, alt, .05);
  else
    define_color_mixed (str, "headerbar_bg_color", &text_bg, alt, .025);
  define_color (str, "headerbar_fg_color", &text_fg);

  define_color_mixed (str, "view_bg_color", &text_bg, &white, is_dark ? .1 : .3);
  define_color (str, "view_fg_color", &text_fg);

  if (get_background (style_scheme, "selection", &color))
    define_color (str, "accent_bg_color", &color);

  if (get_foreground (style_scheme, "selection", &color))
    define_color (str, "accent_fg_color", &color);

  if (get_background (style_scheme, "selection", &color))
    {
      color.alpha = 1;
      define_color_mixed (str, "accent_color", &color, alt, .1);
    }

  if (is_dark)
    g_string_append (str, DARK_CSS_SUFFIX);
  else
    g_string_append (str, LIGHT_CSS_SUFFIX);

  return g_string_free (str, FALSE);
}
