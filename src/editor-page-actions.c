/* editor-page-actions.c
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

#define G_LOG_DOMAIN "editor-page-actions"

#include "editor-page-private.h"
#include "editor-language-dialog.h"

static void
editor_page_actions_search_hide (GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *param)
{
  EditorPage *self = (EditorPage *)widget;

  g_assert (EDITOR_IS_PAGE (self));

  gtk_revealer_set_reveal_child (GTK_REVEALER (self->goto_line_revealer), FALSE);
  _editor_page_hide_search (self);
  editor_page_grab_focus (self);
}

static void
editor_page_actions_search_move_next (GtkWidget  *widget,
                                      const char *action_name,
                                      GVariant   *param)
{
  EditorPage *self = (EditorPage *)widget;

  g_assert (EDITOR_IS_PAGE (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_BOOLEAN));

  _editor_page_move_next_search (self, g_variant_get_boolean (param));
}

static void
editor_page_actions_search_move_previous (GtkWidget  *widget,
                                          const char *action_name,
                                          GVariant   *param)
{
  EditorPage *self = (EditorPage *)widget;

  g_assert (EDITOR_IS_PAGE (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_BOOLEAN));

  _editor_page_move_previous_search (self, g_variant_get_boolean (param));
}

static void
editor_page_actions_replace_one (GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *param)
{
  EditorPage *self = (EditorPage *)widget;

  g_assert (EDITOR_IS_PAGE (self));

  if (_editor_search_bar_get_can_replace (self->search_bar))
    {
      _editor_search_bar_replace (self->search_bar);
      _editor_page_scroll_to_insert (self);
      _editor_search_bar_move_next (self->search_bar, FALSE);
    }
}

static void
editor_page_actions_replace_all (GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *param)
{
  EditorPage *self = (EditorPage *)widget;

  g_assert (EDITOR_IS_PAGE (self));

  if (_editor_search_bar_get_can_replace_all (self->search_bar))
    {
      _editor_search_bar_replace_all (self->search_bar);
      _editor_page_scroll_to_insert (self);
    }
}

static void
editor_page_actions_show_goto_line (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  EditorPage *self = (EditorPage *)widget;
  char str[12];
  guint line, column;

  g_assert (EDITOR_IS_PAGE (self));

  _editor_page_hide_search (self);

  editor_page_get_visual_position (self, &line, &column);
  g_snprintf (str, sizeof str, "%u", line + 1);
  gtk_editable_set_text (GTK_EDITABLE (self->goto_line_entry), str);

  gtk_revealer_set_reveal_child (GTK_REVEALER (self->goto_line_revealer), TRUE);
  gtk_widget_grab_focus (GTK_WIDGET (self->goto_line_entry));
}

static void
editor_page_actions_goto_line (GtkWidget  *widget,
                               const char *action_name,
                               GVariant   *param)
{
  EditorPage *self = (EditorPage *)widget;
  const char *str;
  guint line = 0, column = 0;
  int count;

  g_assert (EDITOR_IS_PAGE (self));

  str = gtk_editable_get_text (GTK_EDITABLE (self->goto_line_entry));
  count = sscanf (str, "%u:%u", &line, &column);

  if (count >= 1)
    {
      EditorDocument *document = editor_page_get_document (self);
      GtkTextIter iter;

      if (line > 0)
        --line;

      if (column > 0)
        --column;

      gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &iter, line, column);
      while (count == 1 &&
             !gtk_text_iter_is_end (&iter) &&
             !gtk_text_iter_ends_line (&iter) &&
             g_unichar_isspace (gtk_text_iter_get_char (&iter)))
        gtk_text_iter_forward_char (&iter);
      gtk_text_buffer_select_range (GTK_TEXT_BUFFER (document), &iter, &iter);
      gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (self->view),
                                    gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (document)),
                                    0.25, TRUE, 1.0, 0.5);
    }

  gtk_revealer_set_reveal_child (GTK_REVEALER (self->goto_line_revealer), FALSE);
  editor_page_grab_focus (self);
}

static void
on_notify_can_move_cb (EditorPage      *self,
                       GParamSpec      *pspec,
                       EditorSearchBar *search_bar)
{
  gboolean can_move;

  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_SEARCH_BAR (search_bar));

  can_move = _editor_search_bar_get_can_move (search_bar);

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "search.move-next", can_move);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "search.move-previous", can_move);
}

static void
on_notify_can_replace_cb (EditorPage      *self,
                          GParamSpec      *pspec,
                          EditorSearchBar *search_bar)
{
  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_SEARCH_BAR (search_bar));

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "search.replace-one",
                                 _editor_search_bar_get_can_replace (search_bar));
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "search.replace-all",
                                 _editor_search_bar_get_can_replace_all (search_bar));
}

void
_editor_page_class_actions_init (EditorPageClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_install_action (widget_class, "page.show-goto-line", NULL,
                                   editor_page_actions_show_goto_line);
  gtk_widget_class_install_action (widget_class, "page.goto-line", NULL,
                                   editor_page_actions_goto_line);
  gtk_widget_class_install_action (widget_class, "search.hide", NULL,
                                   editor_page_actions_search_hide);
  gtk_widget_class_install_action (widget_class, "search.move-next", "b",
                                   editor_page_actions_search_move_next);
  gtk_widget_class_install_action (widget_class, "search.move-previous", "b",
                                   editor_page_actions_search_move_previous);
  gtk_widget_class_install_action (widget_class, "search.replace-one", NULL,
                                   editor_page_actions_replace_one);
  gtk_widget_class_install_action (widget_class, "search.replace-all", NULL,
                                   editor_page_actions_replace_all);
}

void
_editor_page_actions_init (EditorPage *self)
{
  g_signal_connect_object (self->search_bar,
                           "notify::can-move",
                           G_CALLBACK (on_notify_can_move_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->search_bar,
                           "notify::can-replace",
                           G_CALLBACK (on_notify_can_replace_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->search_bar,
                           "notify::can-replace-all",
                           G_CALLBACK (on_notify_can_replace_cb),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "search.move-next", FALSE);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "search.move-previous", FALSE);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "search.replace-all", FALSE);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "search.replace-one", FALSE);
}

void
_editor_page_actions_bind_settings (EditorPage         *self,
                                    EditorPageSettings *settings)
{
  GSimpleActionGroup *group;
  static const char *props[] = {
    "auto-indent",
    "indent-style",
    "indent-width",
    "tab-width",
  };

  g_assert (EDITOR_IS_PAGE (self));
  g_assert (!settings || EDITOR_IS_PAGE_SETTINGS (settings));

  group = g_simple_action_group_new ();
  if (settings != NULL)
    {
      for (guint i = 0; i < G_N_ELEMENTS (props); i++)
        {
          g_autoptr(GPropertyAction) action = g_property_action_new (props[i], settings, props[i]);
          g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
        }
    }
  gtk_widget_insert_action_group (GTK_WIDGET (self), "view", G_ACTION_GROUP (group));
  g_clear_object (&group);
}
