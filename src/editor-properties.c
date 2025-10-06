/*
 * editor-properties.c
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include <glib/gi18n.h>

#include "editor-document.h"
#include "editor-document-statistics.h"
#include "editor-indent-model.h"
#include "editor-page-private.h"
#include "editor-properties.h"
#include "editor-source-view.h"

struct _EditorProperties
{
  GObject        parent_instance;
  GSignalGroup  *buffer_signals;
  GSignalGroup  *view_signals;
  EditorPage    *page;
  GtkAdjustment *indent_width;
};

enum {
  PROP_0,
  PROP_AUTO_INDENT,
  PROP_CAN_OPEN,
  PROP_DIRECTORY,
  PROP_ENCODING,
  PROP_ENCODING_INDEX,
  PROP_INDENT_STYLE_INDEX,
  PROP_INDENT_WIDTH,
  PROP_LANGUAGE,
  PROP_LANGUAGE_NAME,
  PROP_NAME,
  PROP_NEWLINE_TYPE,
  PROP_NEWLINE_INDEX,
  PROP_PAGE,
  PROP_STATISTICS,
  PROP_TAB_WIDTH,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorProperties, editor_properties, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static void
editor_properties_notify_auto_indent_cb (EditorProperties *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_INDENT]);
}

static void
editor_properties_notify_encoding_cb (EditorProperties *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENCODING]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENCODING_INDEX]);
}

static void
editor_properties_notify_language_cb (EditorProperties *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LANGUAGE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LANGUAGE_NAME]);
}

static void
editor_properties_notify_insert_spaces_instead_of_tabs_cb (EditorProperties *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INDENT_STYLE_INDEX]);
}

static void
editor_properties_notify_newline_type_cb (EditorProperties *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NEWLINE_INDEX]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NEWLINE_TYPE]);
}

static void
editor_properties_notify_file_cb (EditorProperties *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAN_OPEN]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTORY]);
}

static guint
editor_properties_get_encoding_index (EditorProperties *self)
{
  g_autoptr(EditorEncoding) encoding = editor_properties_dup_encoding (self);

  return encoding ? editor_encoding_get_index (encoding) : GTK_INVALID_LIST_POSITION;
}

static void
editor_properties_set_encoding_index (EditorProperties *self,
                                      guint             index)
{
  g_autoptr(EditorEncodingModel) model = editor_encoding_model_new ();
  g_autoptr(EditorEncoding) encoding = g_list_model_get_item (G_LIST_MODEL (model), index);

  if (self->page != NULL)
    editor_document_set_encoding (self->page->document, encoding);
}

static guint
editor_properties_get_newline_index (EditorProperties *self)
{
  g_autoptr(EditorNewline) newline = editor_properties_dup_newline_type (self);

  return newline ? editor_newline_get_index (newline) : GTK_INVALID_LIST_POSITION;
}

static void
editor_properties_set_newline_index (EditorProperties *self,
                                     guint             index)
{
  g_autoptr(EditorNewlineModel) model = editor_newline_model_new ();
  g_autoptr(EditorNewline) newline = g_list_model_get_item (G_LIST_MODEL (model), index);

  if (self->page != NULL && newline != NULL)
    _editor_document_set_newline_type (self->page->document, editor_newline_get_newline_type (newline));
}

static guint
editor_properties_get_indent_style_index (EditorProperties *self)
{
  gboolean value;

  if (self->page == NULL)
    return GTK_INVALID_LIST_POSITION;

  value = gtk_source_view_get_insert_spaces_instead_of_tabs (GTK_SOURCE_VIEW (self->page->view));

  return editor_indent_get_index_for (!!value);
}

static void
editor_properties_set_indent_style_index (EditorProperties *self,
                                          guint             index)
{
  gboolean insert_spaces;

  if (self->page == NULL)
    return;

  insert_spaces = index == editor_indent_get_index_for (TRUE);

  gtk_source_view_set_insert_spaces_instead_of_tabs (GTK_SOURCE_VIEW (self->page->view), insert_spaces);
}

static void
editor_properties_finalize (GObject *object)
{
  EditorProperties *self = (EditorProperties *)object;

  g_clear_object (&self->buffer_signals);
  g_clear_object (&self->view_signals);
  g_clear_object (&self->page);
  g_clear_object (&self->indent_width);

  G_OBJECT_CLASS (editor_properties_parent_class)->finalize (object);
}

static void
editor_properties_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EditorProperties *self = EDITOR_PROPERTIES (object);

  switch (prop_id)
    {
    case PROP_AUTO_INDENT:
      g_value_set_boolean (value, editor_properties_get_auto_indent (self));
      break;

    case PROP_CAN_OPEN:
      g_value_set_boolean (value, editor_properties_get_can_open (self));
      break;

    case PROP_DIRECTORY:
      g_value_take_string (value, editor_properties_dup_directory (self));
      break;

    case PROP_ENCODING:
      g_value_take_object (value, editor_properties_dup_encoding (self));
      break;

    case PROP_ENCODING_INDEX:
      g_value_set_uint (value, editor_properties_get_encoding_index (self));
      break;

    case PROP_INDENT_STYLE_INDEX:
      g_value_set_uint (value, editor_properties_get_indent_style_index (self));
      break;

    case PROP_INDENT_WIDTH:
      g_value_take_object (value, editor_properties_dup_indent_width (self));
      break;

    case PROP_LANGUAGE:
      g_value_take_object (value, editor_properties_dup_language (self));
      break;

    case PROP_LANGUAGE_NAME:
      g_value_take_string (value, editor_properties_dup_language_name (self));
      break;

    case PROP_NAME:
      g_value_take_string (value, editor_properties_dup_name (self));
      break;

    case PROP_NEWLINE_INDEX:
      g_value_set_uint (value, editor_properties_get_newline_index (self));
      break;

    case PROP_NEWLINE_TYPE:
      g_value_take_object (value, editor_properties_dup_newline_type (self));
      break;

    case PROP_PAGE:
      g_value_take_object (value, editor_properties_dup_page (self));
      break;

    case PROP_STATISTICS:
      g_value_take_object (value, editor_properties_dup_statistics (self));
      break;

    case PROP_TAB_WIDTH:
      g_value_take_object (value, editor_properties_dup_tab_width (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_properties_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  EditorProperties *self = EDITOR_PROPERTIES (object);

  switch (prop_id)
    {
    case PROP_AUTO_INDENT:
      editor_properties_set_auto_indent (self, g_value_get_boolean (value));
      break;

    case PROP_ENCODING:
      editor_properties_set_encoding (self, g_value_get_object (value));
      break;

    case PROP_ENCODING_INDEX:
      editor_properties_set_encoding_index (self, g_value_get_uint (value));
      break;

    case PROP_INDENT_STYLE_INDEX:
      editor_properties_set_indent_style_index (self, g_value_get_uint (value));
      break;

    case PROP_LANGUAGE:
      editor_properties_set_language (self, g_value_get_object (value));
      break;

    case PROP_NEWLINE_INDEX:
      editor_properties_set_newline_index (self, g_value_get_uint (value));
      break;

    case PROP_NEWLINE_TYPE:
      editor_properties_set_newline_type (self, g_value_get_object (value));
      break;

    case PROP_PAGE:
      editor_properties_set_page (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_properties_class_init (EditorPropertiesClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = editor_properties_finalize;
  object_class->get_property = editor_properties_get_property;
  object_class->set_property = editor_properties_set_property;

  properties[PROP_AUTO_INDENT] =
    g_param_spec_boolean ("auto-indent", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_CAN_OPEN] =
    g_param_spec_boolean ("can-open", NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_DIRECTORY] =
    g_param_spec_string ("directory", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_ENCODING] =
    g_param_spec_object ("encoding", NULL, NULL,
                         EDITOR_TYPE_ENCODING,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_ENCODING_INDEX] =
    g_param_spec_uint ("encoding-index", NULL, NULL,
                       0, G_MAXUINT, GTK_INVALID_LIST_POSITION,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_INDENT_STYLE_INDEX] =
    g_param_spec_uint ("indent-style-index", NULL, NULL,
                       0, G_MAXUINT, GTK_INVALID_LIST_POSITION,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_INDENT_WIDTH] =
    g_param_spec_object ("indent-width", NULL, NULL,
                         GTK_TYPE_ADJUSTMENT,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_LANGUAGE] =
    g_param_spec_object ("language", NULL, NULL,
                         GTK_SOURCE_TYPE_LANGUAGE,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_LANGUAGE_NAME] =
    g_param_spec_string ("language-name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_NEWLINE_INDEX] =
    g_param_spec_uint ("newline-index", NULL, NULL,
                       0, G_MAXUINT, GTK_INVALID_LIST_POSITION,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_NEWLINE_TYPE] =
    g_param_spec_object ("newline-type", NULL, NULL,
                         EDITOR_TYPE_NEWLINE,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_PAGE] =
    g_param_spec_object ("page", NULL, NULL,
                         EDITOR_TYPE_PAGE,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_STATISTICS] =
    g_param_spec_object ("statistics", NULL, NULL,
                         EDITOR_TYPE_DOCUMENT_STATISTICS,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_TAB_WIDTH] =
    g_param_spec_object ("tab-width", NULL, NULL,
                         GTK_TYPE_ADJUSTMENT,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_properties_init (EditorProperties *self)
{
  self->buffer_signals = g_signal_group_new (EDITOR_TYPE_DOCUMENT);
  g_signal_group_connect_object (self->buffer_signals,
                                 "notify::language",
                                 G_CALLBACK (editor_properties_notify_language_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->buffer_signals,
                                 "notify::encoding",
                                 G_CALLBACK (editor_properties_notify_encoding_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->buffer_signals,
                                 "notify::newline-type",
                                 G_CALLBACK (editor_properties_notify_newline_type_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->buffer_signals,
                                 "notify::file",
                                 G_CALLBACK (editor_properties_notify_file_cb),
                                 self,
                                 G_CONNECT_SWAPPED);

  self->view_signals = g_signal_group_new (EDITOR_TYPE_SOURCE_VIEW);
  g_signal_group_connect_object (self->view_signals,
                                 "notify::auto-indent",
                                 G_CALLBACK (editor_properties_notify_auto_indent_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->view_signals,
                                 "notify::insert-spaces-instead-of-tabs",
                                 G_CALLBACK (editor_properties_notify_insert_spaces_instead_of_tabs_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
}

EditorProperties *
editor_properties_new (void)
{
  return g_object_new (EDITOR_TYPE_PROPERTIES, NULL);
}

EditorPage *
editor_properties_dup_page (EditorProperties *self)
{
  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), NULL);

  return self->page ? g_object_ref (self->page) : NULL;
}

void
editor_properties_set_page (EditorProperties *self,
                            EditorPage       *page)
{
  g_return_if_fail (EDITOR_IS_PROPERTIES (self));
  g_return_if_fail (!page || EDITOR_IS_PAGE (page));

  if (g_set_object (&self->page, page))
    {
      EditorSourceView *view = NULL;
      EditorDocument *document = NULL;

      if (page != NULL)
        {
          document = page->document;
          view = EDITOR_SOURCE_VIEW (page->view);
        }

      g_signal_group_set_target (self->buffer_signals, document);
      g_signal_group_set_target (self->view_signals, view);

      g_clear_object (&self->indent_width);

      for (guint i = 1; i < N_PROPS; i++)
        g_object_notify_by_pspec (G_OBJECT (self), properties[i]);
    }
}

EditorDocumentStatistics *
editor_properties_dup_statistics (EditorProperties *self)
{
  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), NULL);

  if (self->page != NULL)
    {
      EditorDocument *document = editor_page_get_document (self->page);

      if (document != NULL)
        return editor_document_load_statistics (document);
    }

  return editor_document_statistics_new (NULL);
}

gboolean
editor_properties_get_auto_indent (EditorProperties *self)
{
  g_autoptr(EditorSourceView) view = NULL;

  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), FALSE);

  if ((view = g_signal_group_dup_target (self->view_signals)))
    return gtk_source_view_get_auto_indent (GTK_SOURCE_VIEW (view));

  return FALSE;
}

void
editor_properties_set_auto_indent (EditorProperties *self,
                                   gboolean          auto_indent)
{
  g_autoptr(EditorSourceView) view = NULL;

  g_return_if_fail (EDITOR_IS_PROPERTIES (self));

  if ((view = g_signal_group_dup_target (self->view_signals)))
    gtk_source_view_set_auto_indent (GTK_SOURCE_VIEW (view), auto_indent);
}

void
editor_properties_set_language (EditorProperties  *self,
                                GtkSourceLanguage *language)
{
  g_return_if_fail (EDITOR_IS_PROPERTIES (self));
  g_return_if_fail (!language || GTK_SOURCE_IS_LANGUAGE (language));

  if (self->page != NULL)
    gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (self->page->document), language);
}

GtkSourceLanguage *
editor_properties_dup_language (EditorProperties *self)
{
  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), NULL);

  if (self->page != NULL)
    {
      GtkSourceLanguage *language = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (self->page->document));

      if (language != NULL)
        return g_object_ref (language);
    }

  return NULL;
}

char *
editor_properties_dup_language_name (EditorProperties *self)
{
  g_autoptr(GtkSourceLanguage) language = NULL;

  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), NULL);

  if ((language = editor_properties_dup_language (self)))
    return g_strdup (gtk_source_language_get_name (language));

  return g_strdup (_("Plain Text"));
}

char *
editor_properties_dup_name (EditorProperties *self)
{
  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), NULL);

  if (self->page != NULL)
    {
      GFile *file = editor_document_get_file (self->page->document);
      char *base = NULL;

      if (file == NULL)
        return g_strdup ("—");

      base = g_file_get_basename (file);

      if (!g_utf8_validate (base, -1, NULL))
        {
          char *tmp = g_utf8_make_valid (base, -1);

          g_free (base);
          base = tmp;
        }

      return base;
    }

  return NULL;
}

char *
editor_properties_dup_directory (EditorProperties *self)
{
  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), NULL);

  if (self->page != NULL)
    {
      GFile *file = editor_document_get_file (self->page->document);
      g_autoptr(GFile) parent = NULL;
      g_autofree char *uri = NULL;

      if (!file || !(parent = g_file_get_parent (file)))
        return g_strdup (_("Draft"));

      if (g_file_is_native (parent))
        return g_file_get_path (parent);

      if ((uri = g_file_get_uri (parent)))
        return g_uri_unescape_string (uri, NULL);

      return NULL;
    }

  return NULL;
}

EditorEncoding *
editor_properties_dup_encoding (EditorProperties *self)
{
  g_autoptr(EditorEncodingModel) model = NULL;
  const GtkSourceEncoding *encoding;
  const char *charset;
  guint position;

  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), NULL);

  if (self->page == NULL)
    return NULL;

  model = editor_encoding_model_new ();
  encoding = _editor_document_get_encoding (self->page->document);

  if (encoding != NULL)
    charset = gtk_source_encoding_get_charset (encoding);
  else
    charset = "UTF-8";

  position = editor_encoding_model_lookup_charset (model, charset);

  if (position != GTK_INVALID_LIST_POSITION)
    return g_list_model_get_item (G_LIST_MODEL (model), position);

  return NULL;
}

void
editor_properties_set_encoding (EditorProperties *self,
                                EditorEncoding   *encoding)
{
  g_return_if_fail (EDITOR_IS_PROPERTIES (self));
  g_return_if_fail (!encoding || EDITOR_IS_ENCODING (encoding));

  if (self->page == NULL)
    return;

  editor_document_set_encoding (self->page->document, encoding);
}

EditorNewline *
editor_properties_dup_newline_type (EditorProperties *self)
{
  GtkSourceNewlineType type;
  EditorNewline *newline;

  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), NULL);

  if (self->page == NULL)
    return NULL;

  type = _editor_document_get_newline_type (self->page->document);

  if ((newline = editor_newline_model_get (NULL, type)))
    return g_object_ref (newline);

  return NULL;
}

void
editor_properties_set_newline_type (EditorProperties *self,
                                    EditorNewline    *newline)
{
  GtkSourceNewlineType newline_type;

  g_return_if_fail (EDITOR_IS_PROPERTIES (self));
  g_return_if_fail (!newline || EDITOR_IS_NEWLINE (newline));

  if (self->page == NULL)
    return;

  if (newline != NULL)
    newline_type = editor_newline_get_newline_type (newline);
  else
    newline_type = GTK_SOURCE_NEWLINE_TYPE_DEFAULT;

  _editor_document_set_newline_type (self->page->document, newline_type);
}

GtkAdjustment *
editor_properties_dup_tab_width (EditorProperties *self)
{
  GtkAdjustment *adj;

  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), NULL);

  if (self->page == NULL)
    return NULL;

  adj = g_object_new (GTK_TYPE_ADJUSTMENT,
                      "lower", 1.0,
                      "upper", 32.0,
                      "value", 8.0,
                      "step-increment", 1.0,
                      "page-increment", 4.0,
                      NULL);

  g_object_bind_property (self->page->view, "tab-width",
                          adj, "value",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  return g_object_ref_sink (adj);
}

static void notify_indent_width_prop  (EditorSourceView *view,
                                       GParamSpec       *pspec,
                                       GtkAdjustment    *adjustment);
static void notify_indent_width_value (GtkAdjustment    *adjustment,
                                       EditorSourceView *view);

static void
notify_indent_width_value (GtkAdjustment    *adjustment,
                           EditorSourceView *view)
{
  int value;

  g_assert (GTK_IS_ADJUSTMENT (adjustment));
  g_assert (EDITOR_IS_SOURCE_VIEW (view));

  value = (int)gtk_adjustment_get_value (adjustment);

  if (value <= 0 || value > 32)
    value = -1;

  if (value == gtk_source_view_get_tab_width (GTK_SOURCE_VIEW (view)))
    value = -1;

  g_signal_handlers_block_by_func (adjustment, G_CALLBACK (notify_indent_width_value), view);
  g_signal_handlers_block_by_func (view, G_CALLBACK (notify_indent_width_prop), adjustment);

  gtk_source_view_set_indent_width (GTK_SOURCE_VIEW (view), value);

  g_signal_handlers_unblock_by_func (view, G_CALLBACK (notify_indent_width_prop), adjustment);
  g_signal_handlers_unblock_by_func (adjustment, G_CALLBACK (notify_indent_width_value), view);
}

static void
notify_indent_width_prop (EditorSourceView *view,
                          GParamSpec       *pspec,
                          GtkAdjustment    *adjustment)
{
  int value;

  g_assert (GTK_IS_ADJUSTMENT (adjustment));
  g_assert (EDITOR_IS_SOURCE_VIEW (view));

  value = gtk_source_view_get_indent_width (GTK_SOURCE_VIEW (view));

  g_signal_handlers_block_by_func (adjustment, G_CALLBACK (notify_indent_width_value), view);
  g_signal_handlers_block_by_func (view, G_CALLBACK (notify_indent_width_prop), adjustment);

  if (value <= 0)
    value = gtk_source_view_get_tab_width (GTK_SOURCE_VIEW (view));

  gtk_adjustment_set_value (adjustment, value);

  g_signal_handlers_unblock_by_func (view, G_CALLBACK (notify_indent_width_prop), adjustment);
  g_signal_handlers_unblock_by_func (adjustment, G_CALLBACK (notify_indent_width_value), view);
}

GtkAdjustment *
editor_properties_dup_indent_width (EditorProperties *self)
{
  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), NULL);

  if (self->page == NULL)
    return NULL;

  if (self->indent_width == NULL)
    {
      self->indent_width = g_object_new (GTK_TYPE_ADJUSTMENT,
                                         "lower", 1.0,
                                         "upper", 32.0,
                                         "value", 8.0,
                                         "step-increment", 1.0,
                                         "page-increment", 4.0,
                                         NULL);

      if (g_object_is_floating (G_OBJECT (self->indent_width)))
        g_object_ref_sink (self->indent_width);

      g_signal_connect_object (self->indent_width,
                               "value-changed",
                               G_CALLBACK (notify_indent_width_value),
                               self->page->view,
                               0);
      g_signal_connect_object (self->page->view,
                               "notify::indent-width",
                               G_CALLBACK (notify_indent_width_prop),
                               self->indent_width,
                               0);
      g_signal_connect_object (self->page->view,
                               "notify::tab-width",
                               G_CALLBACK (notify_indent_width_prop),
                               self->indent_width,
                               0);

      notify_indent_width_prop (EDITOR_SOURCE_VIEW (self->page->view), NULL, self->indent_width);
    }

  return g_object_ref (self->indent_width);
}

gboolean
editor_properties_get_can_open (EditorProperties *self)
{
  g_return_val_if_fail (EDITOR_IS_PROPERTIES (self), FALSE);

  if (self->page == NULL || self->page->document == NULL)
    return FALSE;

  return NULL != editor_document_get_file (self->page->document);
}
