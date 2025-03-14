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

#include <math.h>

#include "editor-recoloring-private.h"

#define SHARED_CSS \
  "@define-color card_fg_color @window_fg_color;\n" \
  "@define-color headerbar_border_color @window_fg_color;\n" \
  "@define-color popover_fg_color @window_fg_color;\n" \
  "@define-color dialog_fg_color @window_fg_color;\n" \
  "@define-color dark_fill_bg_color @headerbar_bg_color;\n" \
  "@define-color sidebar_backdrop_color @window_bg_color;\n" \
  "@define-color sidebar_fg_color @window_fg_color;\n" \
  "@define-color view_fg_color @window_fg_color;\n"
#define LIGHT_CSS_SUFFIX \
  "@define-color card_bg_color alpha(white, .8);\n"
#define DARK_CSS_SUFFIX \
  "@define-color card_bg_color alpha(white, .08);\n"

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

  if (kind == FOREGROUND && fg && fg_set)
    gdk_rgba_parse (color, fg);
  else if (kind == BACKGROUND && bg && bg_set)
    gdk_rgba_parse (color, bg);
  else
    return FALSE;

  return color->alpha >= .1;
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

static gboolean
get_metadata_color (GtkSourceStyleScheme *scheme,
                    const char           *key,
                    GdkRGBA              *color)
{
  const char *str;

  if ((str = gtk_source_style_scheme_get_metadata (scheme, key)))
    return gdk_rgba_parse (color, str);

  return FALSE;
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
  char levelstr[G_ASCII_DTOSTR_BUF_SIZE];

  g_assert (str != NULL);
  g_assert (name != NULL);
  g_assert (a != NULL);
  g_assert (b != NULL);

  a_str = gdk_rgba_to_string (a);
  b_str = gdk_rgba_to_string (b);

  g_ascii_dtostr (levelstr, sizeof levelstr, level);

  /* truncate */
  levelstr[6] = 0;

  g_string_append_printf (str, "@define-color %s mix(%s,%s,%s);\n", name, a_str, b_str, levelstr);
}

#if 0
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
#endif

gboolean
_editor_source_style_scheme_is_dark (GtkSourceStyleScheme *scheme)
{
  const char *id = gtk_source_style_scheme_get_id (scheme);
  const char *variant = gtk_source_style_scheme_get_metadata (scheme, "variant");
  GdkRGBA text_bg;

  if (g_strcmp0 (variant, "light") == 0)
    return FALSE;
  else if (g_strcmp0 (variant, "dark") == 0)
    return TRUE;
  else if (strstr (id, "-dark") != NULL)
    return TRUE;

  if (get_background (scheme, "text", &text_bg))
    {
      /* http://alienryderflex.com/hsp.html */
      double r = text_bg.red * 255.0;
      double g = text_bg.green * 255.0;
      double b = text_bg.blue * 255.0;
      double hsp = sqrt (0.299 * (r * r) +
                         0.587 * (g * g) +
                         0.114 * (b * b));

      return hsp <= 127.5;
    }

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
  gboolean has_fg;
  gboolean has_bg;

  g_return_val_if_fail (GTK_SOURCE_IS_STYLE_SCHEME (style_scheme), NULL);

  /* Don't restyle Adwaita as we already have it */
  id = gtk_source_style_scheme_get_name (style_scheme);
  if (g_str_has_prefix (id, "Adwaita"))
    return NULL;

  name = gtk_source_style_scheme_get_name (style_scheme);
  is_dark = _editor_source_style_scheme_is_dark (style_scheme);
  alt = is_dark ? &white : &black;

  str = g_string_new (SHARED_CSS);
  g_string_append_printf (str, "/* %s */\n", name);

  /* TODO: Improve error checking and fallbacks */

  has_bg = get_background (style_scheme, "text", &text_bg);
  has_fg = get_foreground (style_scheme, "text", &text_fg);
  get_background (style_scheme, "right-margin", &right_margin);
  right_margin.alpha = 1;

  if (get_metadata_color (style_scheme, "window_bg_color", &color))
    define_color (str, "window_bg_color", &color);
  else if (has_bg && has_fg && is_dark)
    define_color (str, "window_bg_color", &text_bg);
  else if (has_bg && has_fg)
    define_color_mixed (str, "window_bg_color", &text_bg, &text_fg, .03);
  else if (is_dark)
    define_color_mixed (str, "window_bg_color", &text_bg, &white, .025);
  else
    define_color_mixed (str, "window_bg_color", &text_bg, &white, .1);

  if (get_metadata_color (style_scheme, "window_fg_color", &color))
    define_color (str, "window_fg_color", &color);
  else if (has_bg && has_fg)
    define_color (str, "window_fg_color", &text_fg);
  else if (is_dark)
    define_color_mixed (str, "window_fg_color", &text_bg, alt, .05);
  else
    define_color_mixed (str, "window_fg_color", &text_bg, alt, .025);

  if (get_metadata_color (style_scheme, "sidebar_fg_color", &color))
    define_color (str, "sidebar_fg_color", &color);
  else if (get_foreground (style_scheme, "line-numbers", &color))
    define_color (str, "sidebar_fg_color", &color);
  else
    define_color (str, "sidebar_fg_color", &text_fg);

  if (get_metadata_color (style_scheme, "sidebar_bg_color", &color))
    define_color (str, "sidebar_bg_color", &color);
  else if (get_background (style_scheme, "line-numbers", &color))
    define_color (str, "sidebar_bg_color", &color);
  else
    define_color (str, "sidebar_bg_color", &text_bg);

  if (get_metadata_color (style_scheme, "headerbar_bg_color", &color))
    define_color (str, "headerbar_bg_color", &color);
  else if (is_dark)
    define_color_mixed (str, "headerbar_bg_color", &text_bg, &white, .07);
  else
    define_color_mixed (str, "headerbar_bg_color", &text_bg, &white, .25);

  if (get_metadata_color (style_scheme, "headerbar_fg_color", &color))
    define_color (str, "headerbar_fg_color", &color);
  else if (has_bg && has_fg)
    define_color (str, "headerbar_fg_color", &text_fg);
  else if (is_dark)
    define_color_mixed (str, "headerbar_fg_color", &text_bg, alt, .05);
  else
    define_color_mixed (str, "headerbar_fg_color", &text_bg, alt, .025);

  if (get_metadata_color (style_scheme, "popover_bg_color", &color))
    define_color (str, "popover_bg_color", &color);
  else
    define_color_mixed (str, "popover_bg_color", &text_bg, &white, is_dark ? .07 : .25);

  if (get_metadata_color (style_scheme, "popover_fg_color", &color))
    define_color (str, "popover_fg_color", &color);

  if (is_dark)
    define_color_mixed (str, "dialog_bg_color", &text_bg, &white, .07);
  else
    define_color (str, "dialog_bg_color", &text_bg);

  if (is_dark)
    define_color_mixed (str, "view_bg_color", &text_bg, &black, .1);
  else
    define_color_mixed (str, "view_bg_color", &text_bg, &white, .3);

  define_color (str, "view_fg_color", &text_fg);

  if (get_metadata_color (style_scheme, "accent_bg_color", &color) ||
      get_background (style_scheme, "selection", &color))
    define_color (str, "accent_bg_color", &color);

  if (get_metadata_color (style_scheme, "accent_fg_color", &color) ||
      get_foreground (style_scheme, "selection", &color))
    define_color (str, "accent_fg_color", &color);

  if (get_metadata_color (style_scheme, "accent_color", &color))
    {
      define_color (str, "accent_color", &color);
    }
  else if (get_metadata_color (style_scheme, "accent_bg_color", &color) ||
           get_background (style_scheme, "selection", &color))
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

GtkSourceStyleScheme *
_editor_source_style_scheme_get_variant (GtkSourceStyleScheme *scheme,
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
  return scheme;
}

