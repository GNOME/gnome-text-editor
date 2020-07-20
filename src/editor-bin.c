/* editor-bin.c
 *
 * Copyright (C) 2017 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "editor-bin"

#include "config.h"

/**
 * SECTION:editor-bin
 * @title: EditorBin
 *
 * This is just a #GtkBin class that also allows for various styling with
 * CSS over what can be done in GtkBin directly.
 */

#include <string.h>

#include "editor-bin-private.h"

G_DEFINE_TYPE (EditorBin, editor_bin, GTK_TYPE_BIN)

static void
border_sum (GtkBorder       *one,
            const GtkBorder *two)
{
  one->top += two->top;
  one->right += two->right;
  one->bottom += two->bottom;
  one->left += two->left;
}

static void
get_borders (GtkStyleContext *style_context,
             GtkBorder       *borders)
{
  GtkBorder border = { 0 };
  GtkBorder padding = { 0 };
  GtkBorder margin = { 0 };
  GtkStateFlags state;

  g_assert (GTK_IS_STYLE_CONTEXT (style_context));
  g_assert (borders != NULL);

  memset (borders, 0, sizeof *borders);

  state = gtk_style_context_get_state (style_context);

  gtk_style_context_get_border (style_context, state, &border);
  gtk_style_context_get_padding (style_context, state, &padding);
  gtk_style_context_get_margin (style_context, state, &margin);

  border_sum (borders, &padding);
  border_sum (borders, &border);
  border_sum (borders, &margin);
}

static void
subtract_border (GtkAllocation *alloc,
                 GtkBorder     *border)
{
  g_return_if_fail (alloc != NULL);
  g_return_if_fail (border != NULL);

  alloc->x += border->left;
  alloc->y += border->top;
  alloc->width -= (border->left + border->right);
  alloc->height -= (border->top + border->bottom);
}

static gboolean
editor_bin_draw (GtkWidget *widget,
                 cairo_t   *cr)
{
  GtkStyleContext *style_context;
  GtkWidget *child;
  GtkAllocation alloc;
  GtkStateFlags state;
  GtkBorder margin;

  g_assert (EDITOR_IS_BIN (widget));

  gtk_widget_get_allocation (widget, &alloc);
  alloc.x = 0;
  alloc.y = 0;

  style_context = gtk_widget_get_style_context (widget);
  state = gtk_widget_get_state_flags (widget);

  gtk_style_context_get_margin (style_context, state, &margin);
  subtract_border (&alloc, &margin);

  gtk_render_background (style_context, cr, alloc.x, alloc.y, alloc.width, alloc.height);

  child = gtk_bin_get_child (GTK_BIN (widget));
  if (child != NULL)
    gtk_container_propagate_draw (GTK_CONTAINER (widget), child, cr);

  gtk_render_frame (style_context, cr, alloc.x, alloc.y, alloc.width, alloc.height);

  return FALSE;
}

static void
editor_bin_size_allocate (GtkWidget     *widget,
                          GtkAllocation *alloc)
{
  EditorBin *self = (EditorBin *)widget;
  GtkAllocation child_alloc = { 0 };
  GtkWidget *child;

  g_assert (EDITOR_IS_BIN (self));
  g_assert (alloc != NULL);

  child = gtk_bin_get_child (GTK_BIN (self));

  if (child != NULL)
    {
      GtkStyleContext *style_context;
      GtkBorder borders;

      style_context = gtk_widget_get_style_context (widget);
      child_alloc = *alloc;

      if (gtk_widget_get_has_window (widget))
        {
          child_alloc.x = 0;
          child_alloc.y = 0;
        }

      get_borders (style_context, &borders);
      subtract_border (&child_alloc, &borders);
    }

  GTK_WIDGET_CLASS (editor_bin_parent_class)->size_allocate (widget, alloc);

  if (child != NULL)
    gtk_widget_size_allocate (child, &child_alloc);
}

static void
editor_bin_get_preferred_width (GtkWidget *widget,
                                gint      *min_width,
                                gint      *nat_width)
{
  EditorBin *self = (EditorBin *)widget;
  GtkStyleContext *style_context;
  GtkWidget *child;
  GtkBorder borders;

  g_assert (EDITOR_IS_BIN (widget));

  *min_width = 0;
  *nat_width = 0;

  child = gtk_bin_get_child (GTK_BIN (self));
  if (child != NULL)
    gtk_widget_get_preferred_width (child, min_width, nat_width);

  style_context = gtk_widget_get_style_context (widget);
  get_borders (style_context, &borders);

  *min_width += (borders.left + borders.right);
  *nat_width += (borders.left + borders.right);
}

static void
editor_bin_get_preferred_height (GtkWidget *widget,
                                 gint      *min_height,
                                 gint      *nat_height)
{
  EditorBin *self = (EditorBin *)widget;
  GtkStyleContext *style_context;
  GtkWidget *child;
  GtkBorder borders;

  g_assert (EDITOR_IS_BIN (widget));

  *min_height = 0;
  *nat_height = 0;

  child = gtk_bin_get_child (GTK_BIN (self));
  if (child != NULL)
    gtk_widget_get_preferred_height (child, min_height, nat_height);

  style_context = gtk_widget_get_style_context (widget);
  get_borders (style_context, &borders);

  *min_height += (borders.top + borders.bottom);
  *nat_height += (borders.top + borders.bottom);
}

static void
editor_bin_class_init (EditorBinClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->draw = editor_bin_draw;
  widget_class->get_preferred_width = editor_bin_get_preferred_width;
  widget_class->get_preferred_height = editor_bin_get_preferred_height;
  widget_class->size_allocate = editor_bin_size_allocate;
}

static void
editor_bin_init (EditorBin *self)
{
}
