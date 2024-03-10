/*
 * editor-source-map.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include <gtksourceview/gtksource.h>

#include "editor-source-map.h"

struct _EditorSourceMap
{
  GtkWidget     parent_instance;
  GtkSourceMap *map;
  GdkRGBA       background;
};

enum {
  PROP_0,
  PROP_VIEW,
  PROP_BACKGROUND,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorSourceMap, editor_source_map, GTK_TYPE_WIDGET)

static GParamSpec *properties[N_PROPS];

static void
editor_source_map_set_background (EditorSourceMap *self,
                                  const GdkRGBA   *background)
{
  static const GdkRGBA transparent;

  g_assert (EDITOR_IS_SOURCE_MAP (self));

  if (background == NULL)
    background = &transparent;

  if (!gdk_rgba_equal (&self->background, background))
    {
      self->background = *background;
      gtk_widget_queue_draw (GTK_WIDGET (self));
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND]);
    }
}

static void
editor_source_map_snapshot (GtkWidget   *widget,
                            GtkSnapshot *snapshot)
{
  EditorSourceMap *self = (EditorSourceMap *)widget;

  g_assert (GTK_IS_WIDGET (widget));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  if (self->background.alpha > 0)
    {
      GtkStyleContext *style_context;
      GtkBorder padding;

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      style_context = gtk_widget_get_style_context (widget);
      gtk_style_context_get_padding (style_context, &padding);
      G_GNUC_END_IGNORE_DEPRECATIONS

      gtk_snapshot_append_color (snapshot,
                                 &self->background,
                                 &GRAPHENE_RECT_INIT (-padding.left,
                                                      0,
                                                      padding.left + gtk_widget_get_width (widget) + padding.right,
                                                      gtk_widget_get_height (widget)));
    }

  GTK_WIDGET_CLASS (editor_source_map_parent_class)->snapshot (widget, snapshot);
}

static gboolean
style_scheme_to_background (GBinding     *binding,
                            const GValue *from,
                            GValue       *to,
                            gpointer      user_data)
{
  GtkSourceStyleScheme *scheme = g_value_get_object (from);

  if (scheme)
    {
      GtkSourceStyle *style = gtk_source_style_scheme_get_style (scheme, "text");
      g_autofree char *bg = NULL;
      GdkRGBA rgba = {0};

      g_object_get (style, "background", &bg, NULL);

      if (bg)
        gdk_rgba_parse (&rgba, bg);

      g_value_set_boxed (to, &rgba);
    }

  return TRUE;
}

static void
editor_source_map_dispose (GObject *object)
{
  EditorSourceMap *self = (EditorSourceMap *)object;

  g_clear_pointer ((GtkWidget **)&self->map, gtk_widget_unparent);

  G_OBJECT_CLASS (editor_source_map_parent_class)->dispose (object);
}

static void
editor_source_map_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EditorSourceMap *self = EDITOR_SOURCE_MAP (object);

  switch (prop_id)
    {
    case PROP_BACKGROUND:
      g_value_set_boxed (value, &self->background);
      break;

    case PROP_VIEW:
      g_value_set_object (value, gtk_source_map_get_view (self->map));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_source_map_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  EditorSourceMap *self = EDITOR_SOURCE_MAP (object);

  switch (prop_id)
    {
    case PROP_BACKGROUND:
      editor_source_map_set_background (self, g_value_get_boxed (value));
      break;

    case PROP_VIEW:
      {
        GtkSourceView *view = g_value_get_object (value);
        GtkTextBuffer *buffer;

        gtk_source_map_set_view (self->map, view);

        if ((buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view))))
          g_object_bind_property_full (buffer, "style-scheme",
                                       self, "background",
                                       G_BINDING_SYNC_CREATE,
                                       style_scheme_to_background, NULL, NULL, NULL);
        break;
      }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_source_map_class_init (EditorSourceMapClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_source_map_dispose;
  object_class->get_property = editor_source_map_get_property;
  object_class->set_property = editor_source_map_set_property;

  widget_class->snapshot = editor_source_map_snapshot;

  properties[PROP_BACKGROUND] =
    g_param_spec_boxed ("background", NULL, NULL,
                        GDK_TYPE_RGBA,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_VIEW] =
    g_param_spec_object ("view", NULL, NULL,
                         GTK_SOURCE_TYPE_VIEW,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "EditorSourceMap");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-source-map.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorSourceMap, map);
}

static void
editor_source_map_init (EditorSourceMap *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
