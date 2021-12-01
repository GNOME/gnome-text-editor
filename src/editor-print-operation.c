/* editor-print-operation.c
 *
 * Copyright 2015 Paolo Borelli <pborelli@gnome.org>
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

#define G_LOG_DOMAIN "editor-print-operation"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-document-private.h"
#include "editor-print-operation.h"

struct _EditorPrintOperation
{
  GtkPrintOperation         parent_instance;

  GtkSourceView            *view;
  GtkSourcePrintCompositor *compositor;
};

G_DEFINE_TYPE (EditorPrintOperation, editor_print_operation, GTK_TYPE_PRINT_OPERATION)

enum {
  PROP_0,
  PROP_VIEW,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
editor_print_operation_dispose (GObject *object)
{
  EditorPrintOperation *self = EDITOR_PRINT_OPERATION (object);

  g_clear_object (&self->compositor);

  G_OBJECT_CLASS (editor_print_operation_parent_class)->dispose (object);
}

static void
editor_print_operation_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  EditorPrintOperation *self = EDITOR_PRINT_OPERATION (object);

  switch (prop_id)
    {
    case PROP_VIEW:
      g_value_set_object (value, self->view);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_print_operation_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  EditorPrintOperation *self = EDITOR_PRINT_OPERATION (object);

  switch (prop_id)
    {
    case PROP_VIEW:
      self->view = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_print_operation_begin_print (GtkPrintOperation *operation,
                                    GtkPrintContext   *context)
{
  EditorPrintOperation *self = EDITOR_PRINT_OPERATION (operation);
  g_autoptr(GSettings) settings = NULL;
  g_autofree char *custom_font = NULL;
  GtkSourceBuffer *buffer;
  GtkTextTag *spelling_tag;
  guint tab_width;
  gboolean syntax_hl;
  gboolean use_system_font;

  settings = g_settings_new ("org.gnome.TextEditor");
  use_system_font = g_settings_get_boolean (settings, "use-system-font");
  custom_font = g_settings_get_string (settings, "custom-font");

  buffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->view)));

  tab_width = gtk_source_view_get_tab_width (GTK_SOURCE_VIEW (self->view));
  syntax_hl = gtk_source_buffer_get_highlight_syntax (buffer);

  self->compositor = g_object_new (GTK_SOURCE_TYPE_PRINT_COMPOSITOR,
                                   "buffer", buffer,
                                   "tab-width", tab_width,
                                   "highlight-syntax", syntax_hl,
                                   NULL);

  if (!use_system_font)
    {
      gtk_source_print_compositor_set_body_font_name (self->compositor, custom_font);
      gtk_source_print_compositor_set_line_numbers_font_name (self->compositor, custom_font);
      gtk_source_print_compositor_set_header_font_name (self->compositor, custom_font);
      gtk_source_print_compositor_set_footer_font_name (self->compositor, custom_font);
    }

  spelling_tag = _editor_document_get_spelling_tag (EDITOR_DOCUMENT (buffer));
  gtk_source_print_compositor_ignore_tag (self->compositor, spelling_tag);
}

static gboolean
editor_print_operation_paginate (GtkPrintOperation *operation,
                                 GtkPrintContext   *context)
{
  EditorPrintOperation *self = EDITOR_PRINT_OPERATION (operation);
  gboolean finished;

  finished = gtk_source_print_compositor_paginate (self->compositor, context);

  if (finished)
    {
      gint n_pages;

      n_pages = gtk_source_print_compositor_get_n_pages (self->compositor);
      gtk_print_operation_set_n_pages (operation, n_pages);
    }

  return finished;
}

static void
editor_print_operation_draw_page (GtkPrintOperation *operation,
                                      GtkPrintContext   *context,
                                      gint               page_nr)
{
  EditorPrintOperation *self = EDITOR_PRINT_OPERATION (operation);

  gtk_source_print_compositor_draw_page (self->compositor, context, page_nr);
}

static void
editor_print_operation_end_print (GtkPrintOperation *operation,
                                  GtkPrintContext   *context)
{
  EditorPrintOperation *self = EDITOR_PRINT_OPERATION (operation);

  g_clear_object (&self->compositor);
}

static void
editor_print_operation_class_init (EditorPrintOperationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkPrintOperationClass *operation_class = GTK_PRINT_OPERATION_CLASS (klass);

  object_class->dispose = editor_print_operation_dispose;
  object_class->get_property = editor_print_operation_get_property;
  object_class->set_property = editor_print_operation_set_property;

  operation_class->begin_print = editor_print_operation_begin_print;
  operation_class->draw_page = editor_print_operation_draw_page;
  operation_class->end_print = editor_print_operation_end_print;

  properties [PROP_VIEW] =
    g_param_spec_object ("view",
                         "View",
                         "The source view.",
                         GTK_SOURCE_TYPE_VIEW,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static gboolean
paginate_cb (GtkPrintOperation *operation,
             GtkPrintContext   *context,
             gpointer           user_data)
{
  return editor_print_operation_paginate (operation, context);
}

static void
editor_print_operation_init (EditorPrintOperation *self)
{
  /* FIXME: gtk decides to call paginate only if it sees a pending signal
   * handler, even if we override the default handler.
   * So for now we connect to the signal instead of overriding the vfunc
   * See https://bugzilla.gnome.org/show_bug.cgi?id=345345
   */
  g_signal_connect (self, "paginate", G_CALLBACK (paginate_cb), NULL);
}

EditorPrintOperation *
editor_print_operation_new (GtkSourceView *view)
{
  g_assert (GTK_SOURCE_IS_VIEW (view));

  return g_object_new (EDITOR_TYPE_PRINT_OPERATION,
                       "view", view,
                       "allow-async", TRUE,
                       NULL);
}
