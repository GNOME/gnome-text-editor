/* editor-search-bar.c
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

#define G_LOG_DOMAIN "editor-search-bar"

#include "config.h"

#include "editor-page-private.h"
#include "editor-search-bar-private.h"

struct _EditorSearchBar
{
  GtkBin                   parent_instance;

  GtkSourceSearchContext  *context;
  GtkSourceSearchSettings *settings;
  GCancellable            *cancellable;

  GtkEntry                *search_entry;
  GtkEntry                *replace_entry;
  GtkButton               *replace_button;
  GtkButton               *replace_all_button;
  GtkCheckButton          *case_button;
  GtkCheckButton          *regex_button;
  GtkCheckButton          *word_button;
  GtkToggleButton         *options_button;
  GtkBox                  *options_box;
};

G_DEFINE_TYPE (EditorSearchBar, editor_search_bar, GTK_TYPE_BIN)

static void
editor_search_bar_scroll_to_insert (EditorSearchBar *self)
{
  GtkWidget *page;

  g_assert (EDITOR_IS_SEARCH_BAR (self));

  if ((page = gtk_widget_get_ancestor (GTK_WIDGET (self), EDITOR_TYPE_PAGE)))
    _editor_page_scroll_to_insert (EDITOR_PAGE (page));
}

static void
editor_search_bar_search_activate_cb (EditorSearchBar *self,
                                      GtkEntry        *entry)
{
  g_assert (EDITOR_IS_SEARCH_BAR (self));
  g_assert (GTK_IS_ENTRY (entry));

  gtk_widget_activate_action (GTK_WIDGET (self), "search.move-next", NULL);
  gtk_widget_activate_action (GTK_WIDGET (self), "search.hide", NULL);
}

static void
editor_search_bar_move_next_forward_cb (GObject      *object,
                                        GAsyncResult *result,
                                        gpointer      user_data)
{
  GtkSourceSearchContext *context = (GtkSourceSearchContext *)object;
  g_autoptr(EditorSearchBar) self = user_data;
  g_autoptr(GError) error = NULL;
  GtkSourceBuffer *buffer;
  GtkTextIter begin;
  GtkTextIter end;
  gboolean has_wrapped = FALSE;

  g_assert (EDITOR_IS_SEARCH_BAR (self));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (GTK_SOURCE_IS_SEARCH_CONTEXT (context));

  if (!gtk_source_search_context_forward_finish (context, result, &begin, &end, &has_wrapped, &error))
    {
      g_debug ("Search forward error: %s", error->message);
      return;
    }

  buffer = gtk_source_search_context_get_buffer (context);
  gtk_text_buffer_select_range (GTK_TEXT_BUFFER (buffer), &begin, &end);
}

static void
editor_search_bar_move_next_cb (GtkWidget   *widget,
                                const gchar *action_name,
                                GVariant    *parameter)
{
  EditorSearchBar *self = (EditorSearchBar *)widget;
  GtkSourceBuffer *buffer;
  GtkTextIter begin;
  GtkTextIter end;

  g_assert (EDITOR_IS_SEARCH_BAR (self));

  if (self->context == NULL)
    return;

  buffer = gtk_source_search_context_get_buffer (self->context);
  gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (buffer), &begin, &end);
  gtk_text_iter_order (&begin, &end);

  gtk_source_search_context_forward_async (self->context,
                                           &end,
                                           self->cancellable,
                                           editor_search_bar_move_next_forward_cb,
                                           g_object_ref (self));

  editor_search_bar_scroll_to_insert (self);
}

static void
editor_search_bar_move_previous_cb (GtkWidget   *widget,
                                    const gchar *action_name,
                                    GVariant    *parameter)
{
  EditorSearchBar *self = (EditorSearchBar *)widget;
  GtkSourceBuffer *buffer;
  GtkTextIter begin;
  GtkTextIter end;

  g_assert (EDITOR_IS_SEARCH_BAR (self));

  if (self->context == NULL)
    return;

  buffer = gtk_source_search_context_get_buffer (self->context);
  gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (buffer), &begin, &end);
  gtk_text_iter_order (&begin, &end);

  gtk_source_search_context_backward_async (self->context,
                                            &begin,
                                            self->cancellable,
                                            /* XXX: fixme */
                                            editor_search_bar_move_next_forward_cb,
                                            g_object_ref (self));
}

static gboolean
empty_to_null (GBinding     *binding,
               const GValue *from_value,
               GValue       *to_value,
               gpointer      user_data)
{
  const gchar *str = g_value_get_string (from_value);
  if (!str || !*str)
    g_value_set_string (to_value, NULL);
  else
    g_value_set_string (to_value, str);
  return TRUE;
}

static gboolean
null_to_empty (GBinding     *binding,
               const GValue *from_value,
               GValue       *to_value,
               gpointer      user_data)
{
  const gchar *str = g_value_get_string (from_value);
  if (!str || !*str)
    g_value_set_string (to_value, "");
  else
    g_value_set_string (to_value, str);
  return TRUE;
}

static gboolean
editor_search_bar_grab_focus (GtkWidget *widget)
{
  EditorSearchBar *self = (EditorSearchBar *)widget;

  g_assert (EDITOR_IS_SEARCH_BAR (self));

  return gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

static void
editor_search_bar_finalize (GObject *object)
{
  EditorSearchBar *self = (EditorSearchBar *)object;

  g_clear_object (&self->cancellable);
  g_clear_object (&self->context);
  g_clear_object (&self->settings);

  G_OBJECT_CLASS (editor_search_bar_parent_class)->finalize (object);
}

static void
editor_search_bar_class_init (EditorSearchBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = editor_search_bar_finalize;

  widget_class->grab_focus = editor_search_bar_grab_focus;

  gtk_widget_class_set_css_name (widget_class, "searchbar");
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-search-bar.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, replace_all_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, replace_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, replace_entry);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, search_entry);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, case_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, word_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, regex_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, options_box);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, options_button);
  gtk_widget_class_bind_template_callback (widget_class, editor_search_bar_search_activate_cb);

  gtk_widget_class_install_action (widget_class, "search.move-next", NULL, editor_search_bar_move_next_cb);
  gtk_widget_class_install_action (widget_class, "search.move-previous", NULL, editor_search_bar_move_previous_cb);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Escape, 0, "search.hide", NULL);
}

static void
editor_search_bar_init (EditorSearchBar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = gtk_source_search_settings_new ();

  gtk_source_search_settings_set_wrap_around (self->settings, TRUE);

  g_object_bind_property_full (self->settings, "search-text",
                               self->search_entry, "text",
                               G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL,
                               null_to_empty, empty_to_null, NULL, NULL);
  g_object_bind_property (self->settings, "at-word-boundaries",
                          self->word_button, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (self->settings, "regex-enabled",
                          self->regex_button, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (self->settings, "case-sensitive",
                          self->case_button, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (self->options_button, "active",
                          self->options_box, "visible",
                          G_BINDING_SYNC_CREATE);
}

void
_editor_search_bar_set_mode (EditorSearchBar     *self,
                             EditorSearchBarMode  mode)
{
  gboolean is_replace;

  g_return_if_fail (EDITOR_IS_SEARCH_BAR (self));

  is_replace = mode == EDITOR_SEARCH_BAR_MODE_REPLACE;

  gtk_widget_set_visible (GTK_WIDGET (self->replace_entry), is_replace);
  gtk_widget_set_visible (GTK_WIDGET (self->replace_button), is_replace);
  gtk_widget_set_visible (GTK_WIDGET (self->replace_all_button), is_replace);
}

void
_editor_search_bar_attach (EditorSearchBar *self,
                           EditorDocument  *document)
{
  g_return_if_fail (EDITOR_IS_SEARCH_BAR (self));

  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");
  gtk_editable_set_text (GTK_EDITABLE (self->replace_entry), "");

  if (self->context != NULL)
    return;

  self->cancellable = g_cancellable_new ();
  self->context = gtk_source_search_context_new (GTK_SOURCE_BUFFER (document),
                                                 self->settings);
}

void
_editor_search_bar_detach (EditorSearchBar *self)
{
  g_return_if_fail (EDITOR_IS_SEARCH_BAR (self));

  g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->context);
  g_clear_object (&self->cancellable);
}
