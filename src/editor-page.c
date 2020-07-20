/* editor-page.c
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

#define G_LOG_DOMAIN "editor-page"

#include "config.h"

#include <glib/gi18n.h>
#include <gtksourceview/gtksource.h>

#include "editor-animation.h"
#include "editor-binding-group.h"
#include "editor-document-private.h"
#include "editor-page-private.h"
#include "editor-page-settings.h"
#include "editor-path-private.h"
#include "editor-print-operation.h"
#include "editor-search-bar-private.h"
#include "editor-utils-private.h"
#include "editor-window.h"

struct _EditorPage
{
  GtkBin                   parent_instance;

  EditorDocument          *document;
  EditorPageSettings      *settings;
  EditorBindingGroup      *settings_bindings;

  EditorAnimation         *progress_animation;

  GtkOverlay              *overlay;
  GtkScrolledWindow       *scroller;
  GtkSourceView           *view;
  GtkProgressBar          *progress_bar;
  GtkRevealer             *search_revealer;
  EditorSearchBar         *search_bar;
  GtkSourceGutterRenderer *lines_renderer;
};

enum {
  PROP_0,
  PROP_BUSY,
  PROP_CAN_SAVE,
  PROP_DOCUMENT,
  PROP_IS_MODIFIED,
  PROP_SUBTITLE,
  PROP_TITLE,
  PROP_POSITION_LABEL,
  N_PROPS
};

G_DEFINE_TYPE (EditorPage, editor_page, GTK_TYPE_BIN)

static GParamSpec *properties [N_PROPS];

static void
editor_page_set_settings (EditorPage         *self,
                          EditorPageSettings *settings)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_PAGE_SETTINGS (settings));

  if (g_set_object (&self->settings, settings))
    editor_binding_group_set_source (self->settings_bindings, settings);
}

static void
editor_page_document_modified_changed_cb (EditorPage     *self,
                                          EditorDocument *document)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_MODIFIED]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAN_SAVE]);
}

static void
editor_page_document_notify_title_cb (EditorPage     *self,
                                      GParamSpec     *pspec,
                                      EditorDocument *document)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

static void
editor_page_document_notify_busy_cb (EditorPage     *self,
                                     GParamSpec     *pspec,
                                     EditorDocument *document)
{
  gboolean busy;

  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  busy = editor_document_get_busy (document);

  gtk_text_view_set_editable (GTK_TEXT_VIEW (self->view), !busy);

  if (!busy)
    _editor_widget_hide_with_fade (GTK_WIDGET (self->progress_bar));
  else
    gtk_widget_show (GTK_WIDGET (self->progress_bar));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUSY]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAN_SAVE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBTITLE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

static void
editor_page_document_cursor_moved_cb (EditorPage     *self,
                                      EditorDocument *document)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POSITION_LABEL]);
}

static void
editor_page_document_notify_file_cb (EditorPage     *self,
                                     GParamSpec     *pspec,
                                     EditorDocument *document)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBTITLE]);
}

static void
editor_page_document_notify_language_cb (EditorPage     *self,
                                         GParamSpec     *pspec,
                                         EditorDocument *document)
{
  g_autoptr(EditorPageSettings) settings = NULL;

  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  settings = editor_page_settings_new_for_document (document);
  editor_page_set_settings (self, settings);
}

static void
editor_page_notify_busy_progress_cb (EditorPage     *self,
                                     GParamSpec     *pspec,
                                     EditorDocument *document)
{
  gdouble busy_progress;

  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  busy_progress = editor_document_get_busy_progress (document);

  g_clear_pointer (&self->progress_animation, editor_animation_stop);

  if (busy_progress == 0.0)
    gtk_progress_bar_set_fraction (self->progress_bar, 0.0);
  else
    self->progress_animation =
      editor_object_animate_full (self->progress_bar,
                                  EDITOR_ANIMATION_EASE_IN_OUT_CUBIC,
                                  300,
                                  NULL,
                                  (GDestroyNotify) g_nullify_pointer,
                                  &self->progress_animation,
                                  "fraction", busy_progress,
                                  NULL);
}

static void
editor_page_set_document (EditorPage     *self,
                          EditorDocument *document)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (self->document == NULL);
  g_assert (!document || EDITOR_IS_DOCUMENT (document));

  if (g_set_object (&self->document, document))
    {
      g_assert (EDITOR_IS_DOCUMENT (document));

      gtk_text_view_set_buffer (GTK_TEXT_VIEW (self->view),
                                GTK_TEXT_BUFFER (document));
      g_signal_connect_object (document,
                               "notify::title",
                               G_CALLBACK (editor_page_document_notify_title_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (document,
                               "modified-changed",
                               G_CALLBACK (editor_page_document_modified_changed_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (document,
                               "notify::busy",
                               G_CALLBACK (editor_page_document_notify_busy_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (document,
                               "cursor-moved",
                               G_CALLBACK (editor_page_document_cursor_moved_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (document,
                               "notify::file",
                               G_CALLBACK (editor_page_document_notify_file_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (document,
                               "notify::language",
                               G_CALLBACK (editor_page_document_notify_language_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (document,
                               "notify::busy-progress",
                               G_CALLBACK (editor_page_notify_busy_progress_cb),
                               self,
                               G_CONNECT_SWAPPED);
    }
}

static gboolean
boolean_to_left_margin (GBinding     *binding,
                        const GValue *from_value,
                        GValue       *to_value,
                        gpointer      user_data)
{
  if (g_value_get_boolean (from_value))
    g_value_set_int (to_value, 0);
  else
    g_value_set_int (to_value, 16);
  return TRUE;
}

static void
editor_page_constructed (GObject *object)
{
  EditorPage *self = (EditorPage *)object;

  g_assert (EDITOR_IS_PAGE (self));

  G_OBJECT_CLASS (editor_page_parent_class)->constructed (object);

  self->settings_bindings = editor_binding_group_new ();

  editor_binding_group_bind (self->settings_bindings, "insert-spaces-instead-of-tabs",
                             self->view, "insert-spaces-instead-of-tabs",
                             G_BINDING_SYNC_CREATE);
  editor_binding_group_bind (self->settings_bindings, "right-margin-position",
                             self->view, "right-margin-position",
                             G_BINDING_SYNC_CREATE);
  editor_binding_group_bind (self->settings_bindings, "show-line-numbers",
                             self->lines_renderer, "visible",
                             G_BINDING_SYNC_CREATE);
  editor_binding_group_bind (self->settings_bindings, "show-right-margin",
                             self->view, "show-right-margin",
                             G_BINDING_SYNC_CREATE);
  editor_binding_group_bind (self->settings_bindings, "tab-width",
                             self->view, "tab-width",
                             G_BINDING_SYNC_CREATE);
  editor_binding_group_bind_full (self->settings_bindings, "wrap-text",
                                  self->view, "wrap-mode",
                                  G_BINDING_SYNC_CREATE,
                                  _editor_gboolean_to_wrap_mode,
                                  NULL, NULL, NULL);
  editor_binding_group_bind_full (self->settings_bindings, "style-scheme",
                                  self->document, "style-scheme",
                                  G_BINDING_SYNC_CREATE,
                                  _editor_gchararray_to_style_scheme,
                                  NULL, NULL, NULL);

  /* Setup margin tweaks for line numbers */
  editor_binding_group_bind_full (self->settings_bindings, "show-line-numbers",
                                  self->view, "left-margin",
                                  G_BINDING_SYNC_CREATE,
                                  boolean_to_left_margin,
                                  NULL, NULL, NULL);

  editor_page_document_notify_busy_cb (self, NULL, self->document);
  editor_page_document_notify_language_cb (self, NULL, self->document);
}

static void
editor_page_view_focus_enter_cb (EditorPage          *self,
                                 const GdkEventFocus *event,
                                 GtkSourceView       *view)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (GTK_SOURCE_IS_VIEW (view));

  _editor_page_hide_search (self);
}

static void
editor_page_grab_focus (GtkWidget *widget)
{
  EditorPage *self = (EditorPage *)widget;

  g_assert (EDITOR_IS_PAGE (self));

  _editor_page_raise (self);

  gtk_widget_grab_focus (GTK_WIDGET (self->view));
}

static void
editor_page_dispose (GObject *object)
{
  EditorPage *self = (EditorPage *)object;

  g_clear_pointer (&self->progress_animation, editor_animation_stop);

  G_OBJECT_CLASS (editor_page_parent_class)->dispose (object);
}

static void
editor_page_finalize (GObject *object)
{
  EditorPage *self = (EditorPage *)object;

  g_assert (self->progress_animation == NULL);

  g_clear_object (&self->document);
  g_clear_object (&self->settings);
  g_clear_object (&self->settings_bindings);

  G_OBJECT_CLASS (editor_page_parent_class)->finalize (object);
}

static void
editor_page_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  EditorPage *self = EDITOR_PAGE (object);

  switch (prop_id)
    {
    case PROP_BUSY:
      g_value_set_boolean (value, editor_page_get_busy (self));
      break;

    case PROP_CAN_SAVE:
      g_value_set_boolean (value, editor_page_get_can_save (self));
      break;

    case PROP_DOCUMENT:
      g_value_set_object (value, editor_page_get_document (self));
      break;

    case PROP_IS_MODIFIED:
      g_value_set_boolean (value, editor_page_get_is_modified (self));
      break;

    case PROP_TITLE:
      g_value_take_string (value, editor_page_dup_title (self));
      break;

    case PROP_SUBTITLE:
      g_value_take_string (value, editor_page_dup_subtitle (self));
      break;

    case PROP_POSITION_LABEL:
      g_value_take_string (value, editor_page_dup_position_label (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_page_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  EditorPage *self = EDITOR_PAGE (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      editor_page_set_document (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_page_class_init (EditorPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = editor_page_constructed;
  object_class->dispose = editor_page_dispose;
  object_class->finalize = editor_page_finalize;
  object_class->get_property = editor_page_get_property;
  object_class->set_property = editor_page_set_property;

  widget_class->grab_focus = editor_page_grab_focus;

  properties [PROP_BUSY] =
    g_param_spec_boolean ("busy",
                          "Busy",
                          "If the page is busy",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CAN_SAVE] =
    g_param_spec_boolean ("can-save",
                          "Can Save",
                          "If the document can be saved",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DOCUMENT] =
    g_param_spec_object ("document",
                         "Document",
                         "The document to be viewed",
                         EDITOR_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title of the document",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SUBTITLE] =
    g_param_spec_string ("subtitle",
                         "Subitle",
                         "The subtitle of the document",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_IS_MODIFIED] =
    g_param_spec_boolean ("is-modified",
                         "Is Modified",
                         "If the underlying buffer has been modified",
                         FALSE,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_POSITION_LABEL] =
    g_param_spec_string ("position-label",
                         "Position Label",
                         "The visual position label text",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  _editor_page_class_actions_init (klass);

  gtk_widget_class_set_css_name (widget_class, "page");
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-page.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorPage, progress_bar);
  gtk_widget_class_bind_template_child (widget_class, EditorPage, scroller);
  gtk_widget_class_bind_template_child (widget_class, EditorPage, search_bar);
  gtk_widget_class_bind_template_child (widget_class, EditorPage, search_revealer);
  gtk_widget_class_bind_template_child (widget_class, EditorPage, view);
  gtk_widget_class_bind_template_child (widget_class, EditorPage, overlay);

  g_type_ensure (EDITOR_TYPE_SEARCH_BAR);
}

static void
editor_page_init (EditorPage *self)
{
  GtkSourceGutter *gutter;

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (self->view,
                           "focus-in-event",
                           G_CALLBACK (editor_page_view_focus_enter_cb),
                           self,
                           G_CONNECT_SWAPPED);

  /* Force registration of GtkSourceGutterRendererLines private type */
  gtk_source_view_set_show_line_numbers (self->view, TRUE);
  gtk_source_view_set_show_line_numbers (self->view, FALSE);

  self->lines_renderer = g_object_new (g_type_from_name ("GtkSourceGutterRendererLines"),
                                       "alignment-mode", GTK_SOURCE_GUTTER_RENDERER_ALIGNMENT_MODE_FIRST,
                                       "yalign", 0.5,
                                       "xalign", 1.0,
                                       "xpad", 12,
                                       NULL);
  gutter = gtk_source_view_get_gutter (self->view, GTK_TEXT_WINDOW_LEFT);
  gtk_source_gutter_insert (gutter, self->lines_renderer, 0);

  _editor_page_actions_init (self);
}

/**
 * editor_page_get_is_modified:
 * @self: a #EditorPage
 *
 * Gets the #EditorPage:is-modified property.
 *
 * This is %TRUE if the underlying document contents does not match the
 * file on disk. The editor always saves modified files, however they are
 * stored as drafts until the user chooses to save them to disk.
 *
 * This function makes no blocking calls.
 *
 * Returns: %TRUE if the document is different from what was last saved
 *   to the underlying storage.
 */
gboolean
editor_page_get_is_modified (EditorPage *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE (self), FALSE);

  return gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (self->document));
}

/**
 * editor_page_new_for_document:
 * @document: an #EditorDocument
 *
 * Creates a new page using the provided #EditorDocument.
 *
 * Returns: (transfer full): an #EditorPage
 */
EditorPage *
editor_page_new_for_document (EditorDocument *document)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (document), NULL);

  return g_object_new (EDITOR_TYPE_PAGE,
                       "document", document,
                       "visible", TRUE,
                       NULL);
}

/**
 * editor_page_get_document:
 * @self: a #EditorPage
 *
 * Gets the document for the page.
 *
 * Returns: (transfer none): an #EditorDocument
 */
EditorDocument *
editor_page_get_document (EditorPage *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE (self), NULL);

  return self->document;
}

EditorWindow *
_editor_page_get_window (EditorPage *self)
{
  GtkWidget *ancestor;

  g_return_val_if_fail (EDITOR_IS_PAGE (self), NULL);

  ancestor = gtk_widget_get_ancestor (GTK_WIDGET (self), EDITOR_TYPE_WINDOW);
  if (EDITOR_IS_WINDOW (ancestor))
    return EDITOR_WINDOW (ancestor);

  return NULL;
}

/**
 * editor_page_is_active:
 * @self: a #EditorPage
 *
 * Gets if the the page is the currently visible page in the window.
 *
 * Returns: %TRUE if the page is the visible page
 */
gboolean
editor_page_is_active (EditorPage *self)
{
  GtkWidget *notebook;
  gint current_page;

  g_return_val_if_fail (EDITOR_IS_PAGE (self), FALSE);

  return ((notebook = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_NOTEBOOK)) &&
          (-1 != (current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook)))) &&
          (GTK_WIDGET (self) == gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), current_page)));
}

gchar *
_editor_page_dup_title_no_i18n (EditorPage *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE (self), NULL);

  return editor_document_dup_title (self->document);
}

gchar *
editor_page_dup_title (EditorPage *self)
{
  g_autofree gchar *ret = NULL;

  g_return_val_if_fail (EDITOR_IS_PAGE (self), NULL);

  if (!(ret = _editor_page_dup_title_no_i18n (self)))
    return g_strdup (_("New Document"));

  return g_steal_pointer (&ret);
}

gchar *
editor_page_dup_subtitle (EditorPage *self)
{
  g_autoptr(GFile) dir = NULL;
  GFile *file;

  g_return_val_if_fail (EDITOR_IS_PAGE (self), NULL);
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self->document), NULL);

  file = editor_document_get_file (self->document);

  if (file == NULL || !(dir = g_file_get_parent (file)))
    {
      GtkTextIter begin;
      GtkTextIter end;

      gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (self->document), &begin, &end);

      /* No subtitle for initial state */
      if (!gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (self->document)) &&
          gtk_text_iter_equal (&begin, &end))
        return NULL;

      return g_strdup (_("Draft"));
    }

  if (!g_file_is_native (dir))
    return g_file_get_uri (dir);

  return _editor_path_collapse (g_file_peek_path (dir));
}

void
_editor_page_raise (EditorPage *self)
{
  g_autofree gchar *title = NULL;
  GtkWidget *notebook;
  gint page_num;

  g_return_if_fail (EDITOR_IS_PAGE (self));

  title = editor_page_dup_title (self);
  g_debug ("Attempting to raise page: \"%s\"", title);

  if (!(notebook = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_NOTEBOOK)))
    return;

  if (-1 == (page_num = gtk_notebook_page_num (GTK_NOTEBOOK (notebook), GTK_WIDGET (self))))
    return;

  if (page_num != gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook)))
    gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), page_num);
}

static void
editor_page_save_cb (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  EditorDocument *document = (EditorDocument *)object;
  g_autoptr(EditorPage) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (EDITOR_IS_DOCUMENT (document));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (EDITOR_IS_PAGE (self));

  if (_editor_document_save_finish (document, result, &error))
    _editor_document_guess_language_async (document, NULL, NULL, NULL);
}

void
_editor_page_save (EditorPage *self)
{
  g_return_if_fail (EDITOR_IS_PAGE (self));

  _editor_page_raise (self);

  if (editor_document_get_file (self->document) == NULL)
    {
      _editor_page_save_as (self);
      return;
    }

  _editor_document_save_async (self->document,
                               NULL,
                               NULL,
                               editor_page_save_cb,
                               g_object_ref (self));
}

static void
editor_page_save_as_cb (EditorPage           *self,
                        gint                  response_id,
                        GtkFileChooserNative *native)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (GTK_IS_FILE_CHOOSER_NATIVE (native));

  if (response_id == GTK_RESPONSE_ACCEPT)
    {
      g_autoptr(GFile) dest = NULL;

      if ((dest = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (native))))
        _editor_document_save_async (self->document,
                                     dest,
                                     NULL,
                                     editor_page_save_cb,
                                     g_object_ref (self));
    }

  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (native));
}

void
_editor_page_save_as (EditorPage *self)
{
  GtkFileChooserNative *native;
  EditorWindow *window;

  g_return_if_fail (EDITOR_IS_PAGE (self));

  _editor_page_raise (self);

  window = _editor_page_get_window (self);
  native = gtk_file_chooser_native_new (_("Save As"),
                                        GTK_WINDOW (window),
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        _("Save"),
                                        _("Cancel"));

  g_signal_connect_object (native,
                           "response",
                           G_CALLBACK (editor_page_save_as_cb),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_native_dialog_show (GTK_NATIVE_DIALOG (native));
}

gboolean
editor_page_get_can_save (EditorPage *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE (self), FALSE);

  return !editor_document_get_busy (self->document);
}

static void
handle_print_result (EditorPage              *self,
                     GtkPrintOperation       *operation,
                     GtkPrintOperationResult  result)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (GTK_IS_PRINT_OPERATION (operation));

  if (result == GTK_PRINT_OPERATION_RESULT_ERROR)
    {
      g_autoptr(GError) error = NULL;

      gtk_print_operation_get_error (operation, &error);

      g_warning ("%s", error->message);

      /* TODO: Display error message with dialog */
    }
}

static void
print_done (GtkPrintOperation       *operation,
            GtkPrintOperationResult  result,
            gpointer                 user_data)
{
  EditorPage *self = user_data;

  g_assert (GTK_IS_PRINT_OPERATION (operation));
  g_assert (EDITOR_IS_PAGE (self));

  handle_print_result (self, operation, result);

  g_object_unref (operation);
  g_object_unref (self);
}

void
_editor_page_print (EditorPage *self)
{
  g_autoptr(EditorPrintOperation) operation = NULL;
  GtkPrintOperationResult result;
  GtkWidget *toplevel;

  g_return_if_fail (EDITOR_IS_PAGE (self));

  toplevel = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
  operation = editor_print_operation_new (self->view);

  /* keep a ref until "done" is emitted */
  g_object_ref (operation);
  g_signal_connect_after (g_object_ref (operation),
                          "done",
                          G_CALLBACK (print_done),
                          g_object_ref (self));

  result = gtk_print_operation_run (GTK_PRINT_OPERATION (operation),
                                    GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                    GTK_WINDOW (toplevel),
                                    NULL);

  handle_print_result (self, GTK_PRINT_OPERATION (operation), result);
}

void
editor_page_get_visual_position (EditorPage *self,
                                 guint      *line,
                                 guint      *line_column)
{
  GtkTextBuffer *buffer;
  GtkTextIter iter;
  GtkTextMark *mark;

  g_return_if_fail (EDITOR_IS_PAGE (self));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->view));
  mark = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, mark);

  if (line)
    *line = gtk_text_iter_get_line (&iter);

  if (line_column)
    *line_column = gtk_source_view_get_visual_column (self->view, &iter);
}

gchar *
editor_page_dup_position_label (EditorPage *self)
{
  guint line;
  guint column;

  g_return_val_if_fail (EDITOR_IS_PAGE (self), NULL);

  editor_page_get_visual_position (self, &line, &column);

  return g_strdup_printf (_("Ln %u, Col %u"), line + 1, column + 1);
}

gboolean
editor_page_is_draft (EditorPage *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE (self), FALSE);

  return editor_document_get_file (self->document) == NULL;
}

void
_editor_page_copy_all (EditorPage *self)
{
  g_autofree gchar *text = NULL;
  GtkTextIter begin, end;
  GtkClipboard *clipboard;

  g_return_if_fail (EDITOR_IS_PAGE (self));

  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (self->document), &begin, &end);
  text = gtk_text_iter_get_slice (&begin, &end);
  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (self), GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_text (clipboard, text, -1);
}

static void
editor_page_delete_draft_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(EditorPage) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (EDITOR_IS_PAGE (self));

  if (!g_file_delete_finish (file, result, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Failed to remove draft: %s\n", error->message);
      _editor_document_set_draft_id (self->document, NULL);
    }

  _editor_document_load_async (self->document,
                               _editor_page_get_window (self),
                               NULL, NULL, NULL);
}

void
_editor_page_discard_changes (EditorPage *self)
{
  g_autoptr(GFile) draft_file = NULL;

  g_return_if_fail (EDITOR_IS_PAGE (self));

  _editor_page_raise (self);

  draft_file = _editor_document_get_draft_file (self->document);

  g_file_delete_async (draft_file,
                       G_PRIORITY_DEFAULT,
                       NULL,
                       editor_page_delete_draft_cb,
                       g_object_ref (self));
}

gboolean
editor_page_get_can_discard (EditorPage *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE (self), FALSE);

  return editor_page_is_draft (self) &&
         !gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (self->document));
}

gint
_editor_page_position (EditorPage *self)
{
  GtkWidget *notebook;

  g_return_val_if_fail (EDITOR_IS_PAGE (self), -1);

  if ((notebook = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_NOTEBOOK)))
    return gtk_notebook_page_num (GTK_NOTEBOOK (notebook), GTK_WIDGET (self));

  return -1;
}

gboolean
editor_page_get_busy (EditorPage *self)
{
  g_return_val_if_fail (EDITOR_IS_PAGE (self), FALSE);

  return editor_document_get_busy (self->document);
}

static void
_editor_page_set_search_visible (EditorPage          *self,
                                 gboolean             search_visible,
                                 EditorSearchBarMode  mode)
{
  g_return_if_fail (EDITOR_IS_PAGE (self));

  if (search_visible)
    {
      _editor_search_bar_set_mode (self->search_bar, mode);
      _editor_search_bar_attach (self->search_bar, self->document);
    }
  else
    {
      _editor_search_bar_detach (self->search_bar);
    }

  gtk_revealer_set_reveal_child (self->search_revealer, search_visible);

  if (search_visible)
    gtk_widget_grab_focus (GTK_WIDGET (self->search_bar));
}

void
_editor_page_begin_search (EditorPage *self)
{
  g_return_if_fail (EDITOR_IS_PAGE (self));

  _editor_page_set_search_visible (self, TRUE, EDITOR_SEARCH_BAR_MODE_SEARCH);
}

void
_editor_page_begin_replace (EditorPage *self)
{
  g_return_if_fail (EDITOR_IS_PAGE (self));

  _editor_page_set_search_visible (self, TRUE, EDITOR_SEARCH_BAR_MODE_REPLACE);
}

void
_editor_page_hide_search (EditorPage *self)
{
  g_return_if_fail (EDITOR_IS_PAGE (self));

  _editor_page_set_search_visible (self, FALSE, 0);
}

void
_editor_page_scroll_to_insert (EditorPage *self)
{
  GtkTextMark *mark;

  g_return_if_fail (EDITOR_IS_PAGE (self));

  mark = gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (self->document));
  gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (self->view),
                                mark,
                                0.25,
                                TRUE,
                                0.75,
                                0.5);
}
