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

#include <string.h>
#include <math.h>

#include "editor-animation.h"
#include "editor-utils-private.h"

#define FONT_FAMILY  "font-family"
#define FONT_VARIANT "font-variant"
#define FONT_STRETCH "font-stretch"
#define FONT_WEIGHT  "font-weight"
#define FONT_SIZE    "font-size"

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

#ifndef GDK_WINDOWING_QUARTZ
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
  gtk_widget_hide (widget);
  gtk_widget_set_opacity (widget, 1.0);
  g_object_unref (widget);
}

void
_editor_widget_hide_with_fade (GtkWidget *widget)
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
                                         EDITOR_ANIMATION_LINEAR,
                                         1000,
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
_editor_gboolean_to_wrap_mode (GBinding     *binding,
                               const GValue *from_value,
                               GValue       *to_value,
                               gpointer      user_data)
{
  if (g_value_get_boolean (from_value))
    g_value_set_enum (to_value, GTK_WRAP_WORD);
  else
    g_value_set_enum (to_value, GTK_WRAP_NONE);
  return TRUE;
}

static gboolean
editor_action (GtkWidget   *widget,
               const gchar *prefix,
               const gchar *action_name,
               GVariant    *parameter)
{
  GtkWidget *toplevel;
  GApplication *app;
  GActionGroup *group = NULL;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
  g_return_val_if_fail (prefix, FALSE);
  g_return_val_if_fail (action_name, FALSE);

  app = g_application_get_default ();
  toplevel = gtk_widget_get_toplevel (widget);

  while ((group == NULL) && (widget != NULL))
    {
      group = gtk_widget_get_action_group (widget, prefix);

      if G_UNLIKELY (GTK_IS_POPOVER (widget))
        {
          GtkWidget *relative_to;

          relative_to = gtk_popover_get_relative_to (GTK_POPOVER (widget));

          if (relative_to != NULL)
            widget = relative_to;
          else
            widget = gtk_widget_get_parent (widget);
        }
      else
        {
          widget = gtk_widget_get_parent (widget);
        }
    }

  if (!group && g_str_equal (prefix, "win") && G_IS_ACTION_GROUP (toplevel))
    group = G_ACTION_GROUP (toplevel);

  if (!group && g_str_equal (prefix, "app") && G_IS_ACTION_GROUP (app))
    group = G_ACTION_GROUP (app);

  if (group && g_action_group_has_action (group, action_name))
    {
      g_action_group_activate_action (group, action_name, parameter);
      return TRUE;
    }

  if (parameter && g_variant_is_floating (parameter))
    {
      parameter = g_variant_ref_sink (parameter);
      g_variant_unref (parameter);
    }

  g_warning ("Failed to locate action %s.%s", prefix, action_name);

  return FALSE;
}


gboolean
_editor_activate_action (GtkWidget   *widget,
                         const gchar *full_action_name,
                         GVariant    *param)
{
  g_autofree gchar *copy = NULL;
  gchar *dot;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
  g_return_val_if_fail (full_action_name != NULL, FALSE);

  copy = g_strdup (full_action_name);
  dot = strchr (copy, '.');
  if (dot == NULL)
    return FALSE;

  *dot = 0;

  return editor_action (widget, copy, dot + 1, param);
}

gboolean
_editor_action_with_string (GtkWidget   *widget,
                            const gchar *group,
                            const gchar *name,
                            const gchar *param)
{
  g_autoptr(GVariant) variant = NULL;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
  g_return_val_if_fail (group != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (param == NULL)
    param = "";

  if (*param != 0)
    {
      g_autoptr(GError) error = NULL;

      variant = g_variant_parse (NULL, param, NULL, NULL, &error);

      if (variant == NULL)
        {
          g_warning ("can't parse keybinding parameters \"%s\": %s",
                     param, error->message);
          return FALSE;
        }
    }

  return editor_action (widget, group, name, variant);
}
