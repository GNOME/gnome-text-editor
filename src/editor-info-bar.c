/* editor-info-bar.c
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

#define G_LOG_DOMAIN "editor-info-bar"

#include <glib/gi18n.h>

#include "editor-document-private.h"
#include "editor-info-bar-private.h"

struct _EditorInfoBar
{
  GtkWidget       parent_instance;

  EditorDocument *document;

  GtkBox         *box;

  /* Discard widgetry */
  GtkInfoBar     *infobar;
  GtkButton      *close;
  GtkButton      *discard;
  GtkButton      *save;
  GtkLabel       *title;
  GtkLabel       *subtitle;
};

enum {
  PROP_0,
  PROP_DOCUMENT,
  N_PROPS
};

G_DEFINE_TYPE (EditorInfoBar, editor_info_bar, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
editor_info_bar_update (EditorInfoBar *self)
{
  g_assert (EDITOR_IS_INFO_BAR (self));
  g_assert (EDITOR_IS_DOCUMENT (self->document));

  /* Ignore things if we're busy to avoid flapping */
  if (editor_document_get_busy (self->document))
    {
      gtk_info_bar_set_revealed (self->infobar, FALSE);
      return;
    }

  if (editor_document_get_externally_modified (self->document))
    {
      gtk_button_set_label (self->discard, _("_Discard Changes and Reload"));
      gtk_button_set_use_underline (self->discard, TRUE);
      gtk_actionable_set_action_name (GTK_ACTIONABLE (self->discard), "page.discard-changes");
      gtk_label_set_label (self->title, _("File Has Changed on Disk"));
      gtk_label_set_label (self->subtitle, _("The file has been changed by another program."));
      gtk_widget_show (GTK_WIDGET (self->discard));
      gtk_widget_hide (GTK_WIDGET (self->save));
      gtk_info_bar_set_revealed (self->infobar, TRUE);
    }
  else if (_editor_document_get_was_restored (self->document))
    {
      if (editor_document_get_file (self->document) == NULL)
        {
          gtk_button_set_label (self->save, _("Save _As…"));
          gtk_actionable_set_action_name (GTK_ACTIONABLE (self->save), "page.save-as");
          gtk_label_set_label (self->title, _("Document Restored"));
          gtk_label_set_label (self->subtitle, _("Unsaved document has been restored."));
          gtk_widget_hide (GTK_WIDGET (self->discard));
          gtk_widget_show (GTK_WIDGET (self->save));
        }
      else
        {
          gtk_button_set_label (self->save, _("_Save…"));
          gtk_actionable_set_action_name (GTK_ACTIONABLE (self->save), "page.confirm-save");
          gtk_button_set_label (self->discard, _("_Discard…"));
          gtk_actionable_set_action_name (GTK_ACTIONABLE (self->discard), "page.confirm-discard-changes");
          gtk_label_set_label (self->title, _("Draft Changes Restored"));
          gtk_label_set_label (self->subtitle, _("Unsaved changes to the document have been restored."));
          gtk_widget_show (GTK_WIDGET (self->discard));
          gtk_widget_show (GTK_WIDGET (self->save));
        }

      gtk_info_bar_set_revealed (self->infobar, TRUE);
    }
  else
    {
      gtk_info_bar_set_revealed (self->infobar, FALSE);
    }
}

static void
on_notify_cb (EditorInfoBar  *self,
              GParamSpec     *pspec,
              EditorDocument *document)
{
  g_assert (EDITOR_IS_INFO_BAR (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  editor_info_bar_update (self);
}

static void
editor_info_bar_close (GtkWidget   *widget,
                       const gchar *action_name,
                       GVariant    *param)
{
  EditorInfoBar *self = (EditorInfoBar *)widget;

  g_assert (EDITOR_IS_INFO_BAR (self));

  gtk_info_bar_set_revealed (self->infobar, FALSE);
}

static void
on_response_cb (EditorInfoBar *self,
                int            response,
                GtkInfoBar    *infobar)
{
  g_assert (EDITOR_IS_INFO_BAR (self));
  g_assert (GTK_IS_INFO_BAR (infobar));

  gtk_info_bar_set_revealed (infobar, FALSE);
}

static void
editor_info_bar_dispose (GObject *object)
{
  EditorInfoBar *self = (EditorInfoBar *)object;

  g_clear_object (&self->document);
  g_clear_pointer ((GtkWidget **)&self->box, gtk_widget_unparent);

  G_OBJECT_CLASS (editor_info_bar_parent_class)->dispose (object);
}

static void
editor_info_bar_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EditorInfoBar *self = EDITOR_INFO_BAR (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_set_object (value, self->document);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_info_bar_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  EditorInfoBar *self = EDITOR_INFO_BAR (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      if (g_set_object (&self->document, g_value_get_object (value)))
        {
          g_signal_connect_object (self->document,
                                   "notify::busy",
                                   G_CALLBACK (on_notify_cb),
                                   self,
                                   G_CONNECT_SWAPPED);
          g_signal_connect_object (self->document,
                                   "notify::externally-modified",
                                   G_CALLBACK (on_notify_cb),
                                   self,
                                   G_CONNECT_SWAPPED);
        }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_info_bar_class_init (EditorInfoBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_info_bar_dispose;
  object_class->get_property = editor_info_bar_get_property;
  object_class->set_property = editor_info_bar_set_property;

  properties [PROP_DOCUMENT] =
    g_param_spec_object ("document",
                         "Document",
                         "The document to monitor",
                         EDITOR_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-info-bar.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorInfoBar, box);
  gtk_widget_class_bind_template_child (widget_class, EditorInfoBar, discard);
  gtk_widget_class_bind_template_child (widget_class, EditorInfoBar, infobar);
  gtk_widget_class_bind_template_child (widget_class, EditorInfoBar, save);
  gtk_widget_class_bind_template_child (widget_class, EditorInfoBar, subtitle);
  gtk_widget_class_bind_template_child (widget_class, EditorInfoBar, title);
  gtk_widget_class_install_action (widget_class, "infobar.close", NULL, editor_info_bar_close);
}

static void
editor_info_bar_init (EditorInfoBar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (self->infobar,
                           "response",
                           G_CALLBACK (on_response_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

GtkWidget *
_editor_info_bar_new (EditorDocument *document)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (document), NULL);

  return g_object_new (EDITOR_TYPE_INFO_BAR,
                       "document", document,
                       NULL);
}
