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

#include "config.h"

#include <glib/gi18n.h>

#include "editor-document-private.h"
#include "editor-encoding-dialog.h"
#include "editor-info-bar-private.h"
#include "editor-page-private.h"
#include "editor-window.h"

typedef enum
{
  EDITOR_BANNER_NONE,
  EDITOR_BANNER_ERROR,
  EDITOR_BANNER_ENCODING,
  EDITOR_BANNER_CHANGED,
  EDITOR_BANNER_RESTORED,
  EDITOR_BANNER_DRAFT_RESTORED,
} EditorBannerState;

struct _EditorInfoBar
{
  GtkWidget          parent_instance;

  EditorDocument    *document;
  GSignalGroup      *signals;
  AdwBanner         *banner;
  EditorBannerState  state;
};

enum
{
  PROP_0,
  PROP_DOCUMENT,
  N_PROPS
};

G_DEFINE_TYPE (EditorInfoBar, editor_info_bar, GTK_TYPE_WIDGET)

static GParamSpec *properties[N_PROPS];

static void
editor_info_bar_update (EditorInfoBar *self)
{
  g_autoptr(GError) error = NULL;
  EditorBannerState state = EDITOR_BANNER_NONE;
  const char *title = "";
  const char *button = NULL;

  g_assert (EDITOR_IS_INFO_BAR (self));

  if (self->document != NULL && !editor_document_get_busy (self->document))
    {
      g_object_get (self->document,
                    "error", &error,
                    NULL);

      if (g_error_matches (error,
                           GTK_SOURCE_FILE_LOADER_ERROR,
                           GTK_SOURCE_FILE_LOADER_ERROR_CONVERSION_FALLBACK))
        {
          state = EDITOR_BANNER_ENCODING;
          title = _("Some Characters Could Not Be Read");
          button = _("Change _Encoding…");
        }
      else if (error != NULL)
        {
          state = EDITOR_BANNER_ERROR;
          title = _("Could Not Open File");
          button = _("_Review…");
        }
      else if (editor_document_get_externally_modified (self->document))
        {
          state = EDITOR_BANNER_CHANGED;
          title = _("File Has Changed on Disk");
          button = _("_Discard and Reload");
        }
      else if (_editor_document_get_was_restored (self->document))
        {
          if (editor_document_get_file (self->document) == NULL)
            {
              state = EDITOR_BANNER_RESTORED;
              title = _("Document Restored");
            }
          else
            {
              state = EDITOR_BANNER_DRAFT_RESTORED;
              title = _("Draft Changes Restored");
            }

          button = _("_Review…");
        }
    }

  adw_banner_set_title (self->banner, title);
  adw_banner_set_button_label (self->banner, button);
  adw_banner_set_revealed (self->banner, state != EDITOR_BANNER_NONE);

  if (state != self->state &&
      state != EDITOR_BANNER_NONE &&
      gtk_widget_get_mapped (GTK_WIDGET (self)))
    gtk_accessible_announce (GTK_ACCESSIBLE (self),
                             title,
                             GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);

  self->state = state;
}

static void
editor_info_bar_notify_cb (EditorInfoBar  *self,
                           GParamSpec     *pspec,
                           EditorDocument *document)
{
  g_assert (EDITOR_IS_INFO_BAR (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  editor_info_bar_update (self);
}

static void
editor_info_bar_error_response_cb (EditorPage     *page,
                                   const char     *response,
                                   AdwAlertDialog *dialog)
{
  EditorDocument *document;
  EditorWindow *window;
  gboolean suggest_admin = FALSE;

  g_assert (EDITOR_IS_PAGE (page));
  g_assert (ADW_IS_ALERT_DIALOG (dialog));

  document = editor_page_get_document (page);
  window = _editor_page_get_window (page);

  if (window == NULL || editor_document_get_busy (document))
    return;

  if (g_str_equal (response, "retry"))
    {
      _editor_document_load_async (document, window, NULL, NULL, NULL);
    }
  else if (g_str_equal (response, "admin"))
    {
      g_object_get (document,
                    "suggest-admin", &suggest_admin,
                    NULL);

      if (suggest_admin)
        _editor_document_use_admin (document, window);
    }
}

static void
editor_info_bar_restored_response_cb (EditorPage     *page,
                                      const char     *response,
                                      AdwAlertDialog *dialog)
{
  EditorDocument *document;

  g_assert (EDITOR_IS_PAGE (page));
  g_assert (ADW_IS_ALERT_DIALOG (dialog));

  document = editor_page_get_document (page);

  if (g_str_equal (response, "keep"))
    {
      _editor_document_set_was_restored (document, FALSE);
      editor_page_grab_focus (page);
      return;
    }

  if (_editor_page_get_window (page) == NULL || editor_document_get_busy (document))
    return;

  _editor_page_raise (page);

  if (g_str_equal (response, "save-as"))
    _editor_page_save_as (page, NULL);
  else if (g_str_equal (response, "save"))
    gtk_widget_activate_action (GTK_WIDGET (page), "page.confirm-save", NULL);
  else if (g_str_equal (response, "discard"))
    gtk_widget_activate_action (GTK_WIDGET (page), "page.discard-changes", "b", FALSE);
}

static void
editor_info_bar_button_clicked_cb (EditorInfoBar *self)
{
  EditorWindow *window;
  EditorPage *page;
  AdwDialog *dialog;

  g_assert (EDITOR_IS_INFO_BAR (self));

  window = EDITOR_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), EDITOR_TYPE_WINDOW));

  if (window == NULL ||
      !(page = editor_window_get_visible_page (window)) ||
      editor_page_get_document (page) != self->document ||
      editor_document_get_busy (self->document))
    return;

  switch (self->state)
    {
    case EDITOR_BANNER_ERROR:
      {
        g_autoptr(GError) error = NULL;
        gboolean suggest_admin = FALSE;

        g_object_get (self->document,
                      "error", &error,
                      "suggest-admin", &suggest_admin,
                      NULL);

        if (error == NULL)
          return;

        dialog = adw_alert_dialog_new (_("Could Not Open File"), error->message);
        adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                        "close", _("_Close"),
                                        "retry", _("_Retry"),
                                        NULL);
        if (suggest_admin)
          adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog),
                                         "admin",
                                         _("Open As _Administrator"));
        adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "close");
        adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "retry");
        g_signal_connect_object (dialog,
                                 "response",
                                 G_CALLBACK (editor_info_bar_error_response_cb),
                                 page,
                                 G_CONNECT_SWAPPED);
        break;
      }

    case EDITOR_BANNER_RESTORED:
    case EDITOR_BANNER_DRAFT_RESTORED:
      dialog = adw_alert_dialog_new (adw_banner_get_title (self->banner),
                                     self->state == EDITOR_BANNER_RESTORED
                                       ? _("An unsaved document has been restored. You can save it or keep editing.")
                                       : _("Unsaved changes have been restored. You can save or discard them, or keep editing."));
      adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "keep", _("_Keep Editing"));
      if (self->state == EDITOR_BANNER_RESTORED)
        adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "save-as", _("Save _As…"));
      else
        adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                        "discard", _("_Discard…"),
                                        "save", _("_Save…"),
                                        NULL);
      adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "keep");
      adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "keep");
      g_signal_connect_object (dialog,
                               "response",
                               G_CALLBACK (editor_info_bar_restored_response_cb),
                               page,
                               G_CONNECT_SWAPPED);
      break;

    case EDITOR_BANNER_ENCODING:
      dialog = editor_encoding_dialog_new (page);
      break;

    case EDITOR_BANNER_CHANGED:
      _editor_page_discard_changes (page);
      return;

    case EDITOR_BANNER_NONE:
    default:
      return;
    }

  adw_dialog_present (dialog, GTK_WIDGET (window));
}

static void
editor_info_bar_dispose (GObject *object)
{
  EditorInfoBar *self = (EditorInfoBar *)object;

  g_clear_object (&self->signals);
  g_clear_object (&self->document);
  g_clear_pointer ((GtkWidget **)&self->banner, gtk_widget_unparent);
  gtk_widget_dispose_template (GTK_WIDGET (self), EDITOR_TYPE_INFO_BAR);

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
          g_signal_group_set_target (self->signals, self->document);
          editor_info_bar_update (self);
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

  properties[PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         EDITOR_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-info-bar.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorInfoBar, banner);
  gtk_widget_class_bind_template_callback (widget_class, editor_info_bar_button_clicked_cb);
}

static void
editor_info_bar_init (EditorInfoBar *self)
{
  static const char *signals[] = {
    "notify::error",
    "notify::busy",
    "notify::externally-modified",
    "notify::was-restored",
    "notify::file",
  };

  gtk_widget_init_template (GTK_WIDGET (self));

  self->signals = g_signal_group_new (EDITOR_TYPE_DOCUMENT);

  for (guint i = 0; i < G_N_ELEMENTS (signals); i++)
    g_signal_group_connect_swapped (self->signals,
                                    signals[i],
                                    G_CALLBACK (editor_info_bar_notify_cb),
                                    self);
}

GtkWidget *
_editor_info_bar_new (EditorDocument *document)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (document), NULL);

  return g_object_new (EDITOR_TYPE_INFO_BAR,
                       "document", document,
                       NULL);
}
