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

  GtkPageSetup             *page_setup;

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

  g_clear_weak_pointer (&self->view);

  g_clear_object (&self->compositor);
  g_clear_object (&self->page_setup);

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
      g_set_weak_pointer (&self->view, g_value_get_object (value));
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
  g_autoptr(GtkSourceBuffer) alt_buffer = NULL;
  g_autofree char *custom_font = NULL;
  g_autofree char *title = NULL;
  g_autofree char *left = NULL;
  EditorDocument *document;
  GtkSourceBuffer *buffer;
  GtkSourceStyleScheme *scheme;
  GtkSourceStyleSchemeManager *schemes;
  GtkTextTag *spelling_tag;
  GFile *file;
  guint tab_width;
  gboolean syntax_hl;
  gboolean use_system_font;
  gboolean show_line_numbers;

  settings = g_settings_new ("org.gnome.TextEditor");
  use_system_font = g_settings_get_boolean (settings, "use-system-font");
  custom_font = g_settings_get_string (settings, "custom-font");

  document = EDITOR_DOCUMENT (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->view)));
  buffer = GTK_SOURCE_BUFFER (document);

  tab_width = gtk_source_view_get_tab_width (GTK_SOURCE_VIEW (self->view));
  show_line_numbers = gtk_source_view_get_show_line_numbers (self->view);

  syntax_hl = gtk_source_buffer_get_highlight_syntax (buffer);

  /* Possibly use an alternate buffer with the Adwaita style-scheme
   * since that is the only one we can really rely on for printing.
   */
  if (syntax_hl &&
      (scheme = gtk_source_buffer_get_style_scheme (buffer)) &&
      g_strcmp0 ("printing", gtk_source_style_scheme_get_id (scheme)) != 0)
    {
      g_autofree char *text = NULL;
      GtkTextIter begin, end;

      schemes = gtk_source_style_scheme_manager_get_default ();
      scheme = gtk_source_style_scheme_manager_get_scheme (schemes, "printing");

      gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer), &begin, &end);
      text = gtk_text_iter_get_slice (&begin, &end);

      alt_buffer = g_object_new (GTK_SOURCE_TYPE_BUFFER,
                                 "language", gtk_source_buffer_get_language (buffer),
                                 "highlight-syntax", TRUE,
                                 "style-scheme", scheme,
                                 "text", text,
                                 NULL);

      buffer = alt_buffer;
    }

  self->compositor = g_object_new (GTK_SOURCE_TYPE_PRINT_COMPOSITOR,
                                   "buffer", buffer,
                                   "tab-width", tab_width,
                                   "highlight-syntax", syntax_hl,
                                   "print-line-numbers", show_line_numbers,
                                   "print-header", TRUE,
                                   "print-footer", TRUE,
                                   "wrap-mode", GTK_WRAP_WORD_CHAR,
                                   NULL);

  if (!use_system_font)
    {
      gtk_source_print_compositor_set_body_font_name (self->compositor, custom_font);
      gtk_source_print_compositor_set_line_numbers_font_name (self->compositor, custom_font);
      gtk_source_print_compositor_set_header_font_name (self->compositor, custom_font);
      gtk_source_print_compositor_set_footer_font_name (self->compositor, custom_font);
    }

  file = editor_document_get_file (document);
  title = editor_document_dup_title (document);

  if (file == NULL)
    left = g_strdup_printf (_("Draft: %s"), title);
  else if (g_file_is_native (file))
    left = g_strdup_printf (_("File: %s"), title);
  else
    left = _editor_document_dup_uri (document);

  gtk_source_print_compositor_set_header_format (self->compositor, TRUE, left, NULL, NULL);
  gtk_source_print_compositor_set_footer_format (self->compositor,
                                                 TRUE,
                                                 NULL,
                                                 NULL,
                                                 /* Translators: %N is the current page number, %Q is the total
                                                  * number of pages (ex. Page 2 of 10)
                                                  */
                                                 _("Page %N of %Q"));

  if ((gpointer)document == (gpointer)buffer)
    {
      spelling_tag = _editor_document_get_spelling_tag (document);
      gtk_source_print_compositor_ignore_tag (self->compositor, spelling_tag);
    }
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
editor_print_operation_constructed (GObject *object)
{
  EditorPrintOperation *self = (EditorPrintOperation *)object;
  g_autoptr(GtkPrintSettings) print_settings = NULL;
  g_autoptr(GtkPageSetup) page_setup = NULL;
  g_autofree char *print_settings_path = NULL;
  g_autofree char *page_setup_path = NULL;

  G_OBJECT_CLASS (editor_print_operation_parent_class)->constructed (object);

  print_settings_path = g_build_filename (g_get_user_data_dir (),
                                          APP_ID,
                                          "print-settings",
                                          NULL);
  page_setup_path = g_build_filename (g_get_user_data_dir (),
                                      APP_ID,
                                      "page-setup",
                                      NULL);

  if (!(print_settings = gtk_print_settings_new_from_file (print_settings_path, NULL)))
    print_settings = gtk_print_settings_new ();

  if (!(page_setup = gtk_page_setup_new_from_file (page_setup_path, NULL)))
    page_setup = gtk_page_setup_new ();

  gtk_print_operation_set_default_page_setup (GTK_PRINT_OPERATION (self), page_setup);
  gtk_print_operation_set_print_settings (GTK_PRINT_OPERATION (self), print_settings);
}

static void
editor_print_operation_class_init (EditorPrintOperationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkPrintOperationClass *operation_class = GTK_PRINT_OPERATION_CLASS (klass);

  object_class->constructed = editor_print_operation_constructed;
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

gboolean
editor_print_operation_save (EditorPrintOperation  *self,
                             GError               **error)
{
  g_autofree char *state_dir = NULL;
  g_autofree char *page_setup_path = NULL;
  g_autofree char *print_settings_path = NULL;
  GtkPrintSettings *print_settings;
  GtkPageSetup *page_setup;

  g_return_val_if_fail (EDITOR_IS_PRINT_OPERATION (self), FALSE);

  state_dir = g_build_filename (g_get_user_data_dir (), APP_ID, NULL);
  page_setup_path = g_build_filename (state_dir, "page-setup", NULL);
  print_settings_path = g_build_filename (state_dir, "print-settings", NULL);

  g_mkdir_with_parents (state_dir, 0770);

  if ((print_settings = gtk_print_operation_get_print_settings (GTK_PRINT_OPERATION (self))))
    {
      if (!gtk_print_settings_to_file (print_settings, print_settings_path, error))
        return FALSE;
    }

  if ((page_setup = gtk_print_operation_get_default_page_setup (GTK_PRINT_OPERATION (self))))
    {
      if (!gtk_page_setup_to_file (page_setup, page_setup_path, error))
        return FALSE;
    }

  return TRUE;
}
