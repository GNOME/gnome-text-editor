/* editor-utils.c
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

#define G_LOG_DOMAIN "editor-utils"

#include "config.h"

#include <glib/gi18n.h>
#include <string.h>
#include <math.h>

#include "editor-animation.h"
#include "editor-utils-private.h"

#define FONT_FAMILY  "font-family"
#define FONT_VARIANT "font-variant"
#define FONT_STRETCH "font-stretch"
#define FONT_WEIGHT  "font-weight"
#define FONT_SIZE    "font-size"

#define CHOICE_LINE_ENDING "line-ending"

gchar *
_editor_font_description_to_css (const PangoFontDescription *font_desc)
{
  PangoFontMask mask;
  GString *str;

#define ADD_KEYVAL(key,fmt) \
  g_string_append(str,key":"fmt";")
#define ADD_KEYVAL_PRINTF(key,fmt,...) \
  g_string_append_printf(str,key":"fmt";", __VA_ARGS__)

  g_return_val_if_fail (font_desc, NULL);

  str = g_string_new (NULL);

  mask = pango_font_description_get_set_fields (font_desc);

  if ((mask & PANGO_FONT_MASK_FAMILY) != 0)
    {
      const gchar *family;

      family = pango_font_description_get_family (font_desc);
      ADD_KEYVAL_PRINTF (FONT_FAMILY, "\"%s\"", family);
    }

  if ((mask & PANGO_FONT_MASK_STYLE) != 0)
    {
      PangoVariant variant;

      variant = pango_font_description_get_variant (font_desc);

      switch (variant)
        {
        case PANGO_VARIANT_NORMAL:
          ADD_KEYVAL (FONT_VARIANT, "normal");
          break;

        case PANGO_VARIANT_SMALL_CAPS:
          ADD_KEYVAL (FONT_VARIANT, "small-caps");
          break;

#if PANGO_VERSION_CHECK(1, 49, 3)
        case PANGO_VARIANT_ALL_SMALL_CAPS:
          ADD_KEYVAL (FONT_VARIANT, "all-small-caps");
          break;

        case PANGO_VARIANT_PETITE_CAPS:
          ADD_KEYVAL (FONT_VARIANT, "petite-caps");
          break;

        case PANGO_VARIANT_ALL_PETITE_CAPS:
          ADD_KEYVAL (FONT_VARIANT, "all-petite-caps");
          break;

        case PANGO_VARIANT_UNICASE:
          ADD_KEYVAL (FONT_VARIANT, "unicase");
          break;

        case PANGO_VARIANT_TITLE_CAPS:
          ADD_KEYVAL (FONT_VARIANT, "titling-caps");
          break;
#endif

        default:
          break;
        }
    }

  if ((mask & PANGO_FONT_MASK_WEIGHT))
    {
      gint weight;

      weight = pango_font_description_get_weight (font_desc);

      /*
       * WORKAROUND:
       *
       * font-weight with numbers does not appear to be working as expected
       * right now. So for the common (bold/normal), let's just use the string
       * and let gtk warn for the other values, which shouldn't really be
       * used for this.
       */

      switch (weight)
        {
        case PANGO_WEIGHT_SEMILIGHT:
          /*
           * 350 is not actually a valid css font-weight, so we will just round
           * up to 400.
           */
        case PANGO_WEIGHT_NORMAL:
          ADD_KEYVAL (FONT_WEIGHT, "normal");
          break;

        case PANGO_WEIGHT_BOLD:
          ADD_KEYVAL (FONT_WEIGHT, "bold");
          break;

        case PANGO_WEIGHT_THIN:
        case PANGO_WEIGHT_ULTRALIGHT:
        case PANGO_WEIGHT_LIGHT:
        case PANGO_WEIGHT_BOOK:
        case PANGO_WEIGHT_MEDIUM:
        case PANGO_WEIGHT_SEMIBOLD:
        case PANGO_WEIGHT_ULTRABOLD:
        case PANGO_WEIGHT_HEAVY:
        case PANGO_WEIGHT_ULTRAHEAVY:
        default:
          /* round to nearest hundred */
          weight = round (weight / 100.0) * 100;
          ADD_KEYVAL_PRINTF ("font-weight", "%d", weight);
          break;
        }
    }

#ifndef GDK_WINDOWING_MACOS
  /*
   * We seem to get "Condensed" for fonts on the Quartz backend,
   * which is rather annoying as it results in us always hitting
   * fallback (stretch) paths. So let's cheat and just disable
   * stretch support for now on Quartz.
   */
  if ((mask & PANGO_FONT_MASK_STRETCH))
    {
      switch (pango_font_description_get_stretch (font_desc))
        {
        case PANGO_STRETCH_ULTRA_CONDENSED:
          ADD_KEYVAL (FONT_STRETCH, "untra-condensed");
          break;

        case PANGO_STRETCH_EXTRA_CONDENSED:
          ADD_KEYVAL (FONT_STRETCH, "extra-condensed");
          break;

        case PANGO_STRETCH_CONDENSED:
          ADD_KEYVAL (FONT_STRETCH, "condensed");
          break;

        case PANGO_STRETCH_SEMI_CONDENSED:
          ADD_KEYVAL (FONT_STRETCH, "semi-condensed");
          break;

        case PANGO_STRETCH_NORMAL:
          ADD_KEYVAL (FONT_STRETCH, "normal");
          break;

        case PANGO_STRETCH_SEMI_EXPANDED:
          ADD_KEYVAL (FONT_STRETCH, "semi-expanded");
          break;

        case PANGO_STRETCH_EXPANDED:
          ADD_KEYVAL (FONT_STRETCH, "expanded");
          break;

        case PANGO_STRETCH_EXTRA_EXPANDED:
          ADD_KEYVAL (FONT_STRETCH, "extra-expanded");
          break;

        case PANGO_STRETCH_ULTRA_EXPANDED:
          ADD_KEYVAL (FONT_STRETCH, "untra-expanded");
          break;

        default:
          break;
        }
    }
#endif

  if ((mask & PANGO_FONT_MASK_SIZE))
    {
      gint font_size;

      font_size = pango_font_description_get_size (font_desc) / PANGO_SCALE;
      ADD_KEYVAL_PRINTF ("font-size", "%dpt", font_size);
    }

  return g_string_free (str, FALSE);

#undef ADD_KEYVAL
#undef ADD_KEYVAL_PRINTF
}

static void
hide_callback (gpointer data)
{
  GtkWidget *widget = data;

  g_object_set_data (data, "EDITOR_FADE_ANIMATION", NULL);
  gtk_widget_set_visible (widget, FALSE);
  gtk_widget_set_opacity (widget, 1.0);
  g_object_unref (widget);
}

void
_editor_widget_hide_with_fade_delay (GtkWidget *widget,
                                     guint      delay)
{
  GdkFrameClock *frame_clock;
  EditorAnimation *anim;

  g_return_if_fail (GTK_IS_WIDGET (widget));

  if (gtk_widget_get_visible (widget))
    {
      anim = g_object_get_data (G_OBJECT (widget), "EDITOR_FADE_ANIMATION");
      if (anim != NULL)
        editor_animation_stop (anim);

      frame_clock = gtk_widget_get_frame_clock (widget);
      anim = editor_object_animate_full (widget,
                                         EDITOR_ANIMATION_EASE_OUT_CUBIC,
                                         delay,
                                         frame_clock,
                                         hide_callback,
                                         g_object_ref (widget),
                                         "opacity", 0.0,
                                         NULL);
      g_object_set_data_full (G_OBJECT (widget),
                              "EDITOR_FADE_ANIMATION",
                              g_object_ref (anim),
                              g_object_unref);
    }
}

void
_editor_widget_hide_with_fade (GtkWidget *widget)
{
  _editor_widget_hide_with_fade_delay (widget, 1000);
}

gboolean
_editor_gchararray_to_boolean (GBinding     *binding,
                               const GValue *from_value,
                               GValue       *to_value,
                               gpointer      user_data)
{
  const gchar *str = g_value_get_string (from_value);
  g_value_set_boolean (to_value, str && str[0]);
  return TRUE;
}

gboolean
_editor_gchararray_to_style_scheme (GBinding     *binding,
                                    const GValue *from_value,
                                    GValue       *to_value,
                                    gpointer      user_data)
{
  GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default ();
  const gchar *scheme_id = g_value_get_string (from_value);
  if (scheme_id)
    g_value_set_object (to_value, gtk_source_style_scheme_manager_get_scheme (sm, scheme_id));
  return TRUE;
}

gboolean
_editor_gboolean_to_background_pattern (GBinding     *binding,
                                        const GValue *from_value,
                                        GValue       *to_value,
                                        gpointer      user_data)
{
  if (g_value_get_boolean (from_value))
    g_value_set_enum (to_value, GTK_SOURCE_BACKGROUND_PATTERN_TYPE_GRID);
  else
    g_value_set_enum (to_value, GTK_SOURCE_BACKGROUND_PATTERN_TYPE_NONE);
  return TRUE;
}

gboolean
_editor_gboolean_to_scroll_policy (GBinding     *binding,
                                   const GValue *from_value,
                                   GValue       *to_value,
                                   gpointer      user_data)
{
  if (g_value_get_boolean (from_value))
    g_value_set_enum (to_value, GTK_POLICY_EXTERNAL);
  else
    g_value_set_enum (to_value, GTK_POLICY_AUTOMATIC);
  return TRUE;
}

gboolean
_editor_gboolean_to_wrap_mode (GBinding     *binding,
                               const GValue *from_value,
                               GValue       *to_value,
                               gpointer      user_data)
{
  if (g_value_get_boolean (from_value))
    g_value_set_enum (to_value, GTK_WRAP_WORD_CHAR);
  else
    g_value_set_enum (to_value, GTK_WRAP_NONE);
  return TRUE;
}

gchar *
_editor_date_time_format (GDateTime *self)
{
  g_autoptr(GDateTime) now = NULL;
  GTimeSpan diff;
  gint years;

  /*
   * TODO:
   *
   * There is probably a lot more we can do here to be friendly for
   * various locales, but this will get us started.
   */

  g_return_val_if_fail (self != NULL, NULL);

  now = g_date_time_new_now_utc ();
  diff = g_date_time_difference (now, self) / G_USEC_PER_SEC;

  if (diff < 0)
    return g_strdup ("");
  else if (diff < (60 * 45))
    return g_strdup (_("Just now"));
  else if (diff < (60 * 90))
    return g_strdup (_("An hour ago"));
  else if (diff < (60 * 60 * 24 * 2))
    return g_strdup (_("Yesterday"));
  else if (diff < (60 * 60 * 24 * 7))
    return g_date_time_format (self, "%A");
  else if (diff < (60 * 60 * 24 * 365))
    return g_date_time_format (self, "%OB");
  else if (diff < (60 * 60 * 24 * 365 * 1.5))
    return g_strdup (_("About a year ago"));

  years = MAX (2, diff / (60 * 60 * 24 * 365));

  return g_strdup_printf (ngettext ("About %u year ago", "About %u years ago", years), years);
}

static gboolean
revealer_autohide (gpointer data)
{
  GtkRevealer *revealer = data;

  g_assert (GTK_IS_REVEALER (revealer));

  g_object_set_data (G_OBJECT (revealer), "AUTOHIDE_ID", NULL);

  if (gtk_widget_get_parent (GTK_WIDGET (revealer)) != NULL)
    {
      if (!gtk_revealer_get_reveal_child (revealer) &&
          !gtk_revealer_get_child_revealed (revealer))
        gtk_widget_set_visible (GTK_WIDGET (revealer), FALSE);
    }

  return G_SOURCE_REMOVE;
}

static void
revealer_queue_autohide (GtkRevealer *revealer)
{
  guint id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (revealer), "AUTOHIDE_ID"));

  if (id != 0)
    {
      g_source_remove (id);
      id = 0;
    }

  if (gtk_widget_get_parent (GTK_WIDGET (revealer)) != NULL)
    id = g_idle_add_full (G_PRIORITY_HIGH,
                          revealer_autohide,
                          g_object_ref (revealer),
                          g_object_unref);

  g_object_set_data (G_OBJECT (revealer), "AUTOHIDE_ID", GUINT_TO_POINTER (id));

  if ((gtk_revealer_get_reveal_child (revealer) ||
       gtk_revealer_get_child_revealed (revealer)) &&
      !gtk_widget_get_visible (GTK_WIDGET (revealer)))
    gtk_widget_set_visible (GTK_WIDGET (revealer), TRUE);
}

/* Try to get a revealer out of the size request machinery
 * until it is even necessary (ie: displayed).
 */
void
_editor_revealer_auto_hide (GtkRevealer *revealer)
{
  g_return_if_fail (GTK_IS_REVEALER (revealer));

  g_signal_connect (revealer,
                    "notify::reveal-child",
                    G_CALLBACK (revealer_queue_autohide),
                    NULL);
  g_signal_connect (revealer,
                    "notify::child-revealed",
                    G_CALLBACK (revealer_queue_autohide),
                    NULL);
  revealer_queue_autohide (revealer);
}
