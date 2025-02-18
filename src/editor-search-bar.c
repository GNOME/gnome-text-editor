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

#include "editor-enums.h"
#include "editor-focus-chain.h"
#include "editor-page-private.h"
#include "editor-search-bar-private.h"
#include "editor-search-entry-private.h"
#include "editor-utils-private.h"

G_DEFINE_TYPE (EditorSearchBar, editor_search_bar, GTK_TYPE_WIDGET)

typedef struct
{
  GWeakRef wr;
} WeakHold;

static WeakHold *
weak_hold_new (EditorSearchBar *self)
{
  WeakHold *wh = g_new0 (WeakHold, 1);
  g_weak_ref_init (&wh->wr, self);
  return wh;
}

static void
weak_hold_free (WeakHold *wh)
{
  if (wh != NULL)
    {
      g_weak_ref_clear (&wh->wr);
      g_free (wh);
    }
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (WeakHold, weak_hold_free)

enum {
  PROP_0,
  PROP_CAN_MOVE,
  PROP_CAN_REPLACE,
  PROP_CAN_REPLACE_ALL,
  PROP_MODE,
  N_PROPS
};

enum {
  MOVE_NEXT_SEARCH,
  MOVE_PREVIOUS_SEARCH,
  N_SIGNALS
};

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

static void
notify_style_scheme_cb (EditorSearchBar *self,
                        GParamSpec      *pspec,
                        EditorDocument  *document)
{
  GtkTextTagTable *table;
  GtkSourceStyleScheme *scheme;

  g_assert (EDITOR_IS_SEARCH_BAR (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (document));
  scheme = gtk_source_buffer_get_style_scheme (GTK_SOURCE_BUFFER (document));

  if (self->current_tag != NULL)
    gtk_text_tag_table_remove (table, self->current_tag);
  self->current_tag = gtk_text_buffer_create_tag (GTK_TEXT_BUFFER (document), NULL, NULL);

  if (scheme != NULL)
    {
      GtkSourceStyle *style = gtk_source_style_scheme_get_style (scheme, "search-match");

      if (style != NULL)
        {
          g_autoptr(GdkRGBA) rgba = NULL;

          gtk_source_style_apply (style, self->current_tag);

          g_object_get (self->current_tag,
                        "background-rgba", &rgba,
                        NULL);

          if (rgba != NULL)
            {
              rgba->alpha = 1.0;
              g_object_set (self->current_tag,
                            "background-rgba", rgba,
                            NULL);
            }
        }
    }

  gtk_text_tag_set_priority (self->current_tag,
                             gtk_text_tag_table_get_size (table)-1);
}

static void
editor_search_bar_root (GtkWidget *widget)
{
  EditorSearchBar *self = (EditorSearchBar *)widget;
  EditorPage *page;

  g_assert (EDITOR_IS_SEARCH_BAR (self));

  GTK_WIDGET_CLASS (editor_search_bar_parent_class)->root (widget);

  if (!(page = EDITOR_PAGE (gtk_widget_get_ancestor (widget, EDITOR_TYPE_PAGE))))
    return;

  g_signal_connect_object (page->document,
                           "notify::style-scheme",
                           G_CALLBACK (notify_style_scheme_cb),
                           self,
                           G_CONNECT_SWAPPED);

  notify_style_scheme_cb (self, NULL, page->document);
}

static void
editor_search_bar_unroot (GtkWidget *widget)
{
  EditorSearchBar *self = (EditorSearchBar *)widget;

  g_assert (EDITOR_IS_SEARCH_BAR (self));

  self->current_tag = NULL;

  GTK_WIDGET_CLASS (editor_search_bar_parent_class)->unroot (widget);
}

static void
update_properties (EditorSearchBar *self)
{
  gboolean can_move = _editor_search_bar_get_can_move (self);
  gboolean can_replace = _editor_search_bar_get_can_replace (self);
  gboolean can_replace_all = _editor_search_bar_get_can_replace_all (self);
  int occurrence_position = -1;

  if (can_move != self->can_move)
    {
      self->can_move = can_move;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_MOVE]);
    }

  if (can_replace != self->can_replace)
    {
      self->can_replace = can_replace;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_REPLACE]);
    }

  if (can_replace_all != self->can_replace_all)
    {
      self->can_replace_all = can_replace_all;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_REPLACE_ALL]);
    }

  if (self->context != NULL)
    {
      GtkTextBuffer *buffer = GTK_TEXT_BUFFER (gtk_source_search_context_get_buffer (self->context));
      GtkTextIter begin, end;

      gtk_text_buffer_get_bounds (buffer, &begin, &end);
      gtk_text_buffer_remove_tag (buffer, self->current_tag, &begin, &end);

      if (gtk_text_buffer_get_selection_bounds (buffer, &begin, &end))
        {
          occurrence_position = gtk_source_search_context_get_occurrence_position (self->context, &begin, &end);

          if (occurrence_position > 0)
            {
              gtk_text_buffer_apply_tag (buffer, self->current_tag, &begin, &end);
              gtk_text_tag_set_priority (self->current_tag,
                                         gtk_text_tag_table_get_size (gtk_text_buffer_get_tag_table (buffer))-1);
            }
        }
    }

  editor_search_entry_set_occurrence_position (self->search_entry, occurrence_position);
}

static void
editor_search_bar_scroll_to_insert (EditorSearchBar *self)
{
  GtkWidget *page;

  g_assert (EDITOR_IS_SEARCH_BAR (self));

  if ((page = gtk_widget_get_ancestor (GTK_WIDGET (self), EDITOR_TYPE_PAGE)))
    _editor_page_scroll_to_insert (EDITOR_PAGE (page));
}

static void
editor_search_bar_move_next_forward_cb (GObject      *object,
                                        GAsyncResult *result,
                                        gpointer      user_data)
{
  GtkSourceSearchContext *context = (GtkSourceSearchContext *)object;
  g_autoptr(WeakHold) wh = user_data;
  g_autoptr(EditorSearchBar) self = NULL;
  g_autoptr(GError) error = NULL;
  GtkSourceBuffer *buffer;
  GtkTextIter begin;
  GtkTextIter end;
  gboolean has_wrapped = FALSE;

  g_assert (wh != NULL);
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (GTK_SOURCE_IS_SEARCH_CONTEXT (context));

  if (!(self = g_weak_ref_get (&wh->wr)))
    return;

  g_assert (EDITOR_IS_SEARCH_BAR (self));

  if (!gtk_source_search_context_forward_finish (context, result, &begin, &end, &has_wrapped, &error))
    {
      if (error != NULL)
        g_debug ("Search forward error: %s", error->message);
      return;
    }

  buffer = gtk_source_search_context_get_buffer (context);
  gtk_text_buffer_select_range (GTK_TEXT_BUFFER (buffer), &begin, &end);
  editor_search_bar_scroll_to_insert (self);

  if (self->hide_after_move)
    gtk_widget_activate_action (GTK_WIDGET (self), "search.hide", NULL);
}

static void
reset_cancellable (EditorSearchBar *self)
{
  g_assert (EDITOR_IS_SEARCH_BAR (self));

  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();
}

void
_editor_search_bar_move_next (EditorSearchBar *self,
                              gboolean         hide_after_move)
{
  GtkSourceBuffer *buffer;
  GtkTextIter begin;
  GtkTextIter end;

  g_assert (EDITOR_IS_SEARCH_BAR (self));

  if (self->context == NULL)
    return;

  self->hide_after_move = !!hide_after_move;
  self->jump_back_on_hide = FALSE;

  buffer = gtk_source_search_context_get_buffer (self->context);
  gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (buffer), &begin, &end);
  gtk_text_iter_order (&begin, &end);

  reset_cancellable (self);

  gtk_source_search_context_forward_async (self->context,
                                           &end,
                                           self->cancellable,
                                           editor_search_bar_move_next_forward_cb,
                                           weak_hold_new (self));
}

void
_editor_search_bar_move_previous (EditorSearchBar *self,
                                  gboolean         hide_after_move)
{
  GtkSourceBuffer *buffer;
  GtkTextIter begin;
  GtkTextIter end;

  g_assert (EDITOR_IS_SEARCH_BAR (self));

  if (self->context == NULL)
    return;

  self->hide_after_move = !!hide_after_move;
  self->jump_back_on_hide = FALSE;

  buffer = gtk_source_search_context_get_buffer (self->context);
  gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (buffer), &begin, &end);
  gtk_text_iter_order (&begin, &end);

  reset_cancellable (self);

  gtk_source_search_context_backward_async (self->context,
                                            &begin,
                                            self->cancellable,
                                            /* XXX: fixme */
                                            editor_search_bar_move_next_forward_cb,
                                            weak_hold_new (self));
}

static void
search_activate (GtkWidget  *widget,
                 const char *action_name,
                 GVariant   *param)
{
  EditorSearchBar *self = EDITOR_SEARCH_BAR (widget);

  if (self->context == NULL)
    return;

  if (editor_search_entry_get_occurrence_position (self->search_entry) > 0)
    gtk_widget_activate_action (GTK_WIDGET (self), "search.hide", NULL);
  else
    _editor_search_bar_move_next (self, TRUE);
}

static void
search_cancel (GtkWidget  *widget,
               const char *action_name,
               GVariant   *param)
{
  EditorSearchBar *self = EDITOR_SEARCH_BAR (widget);
  EditorDocument *document;
  EditorPage *page;

  if (!(page = EDITOR_PAGE (gtk_widget_get_ancestor (widget, EDITOR_TYPE_PAGE))))
    return;

  document = editor_page_get_document (page);
  _editor_document_restore_insert_mark (document);

  gtk_widget_activate_action (GTK_WIDGET (self), "search.hide", NULL);

  _editor_page_scroll_to_insert (page);
}

static gboolean
text_to_search_text (GBinding     *binding,
                     const GValue *from_value,
                     GValue       *to_value,
                     gpointer      user_data)
{
  EditorSearchBar *self = user_data;
  const gchar *str = g_value_get_string (from_value);

  if (!str || gtk_source_search_settings_get_regex_enabled (self->settings))
    g_value_set_string (to_value, str);
  else
    g_value_take_string (to_value, gtk_source_utils_unescape_search_text (str));

  return TRUE;
}

static gboolean
search_text_to_text (GBinding     *binding,
                     const GValue *from_value,
                     GValue       *to_value,
                     gpointer      user_data)
{
  EditorSearchBar *self = user_data;
  const gchar *str = g_value_get_string (from_value);

  if (str == NULL)
    str = "";

  if (gtk_source_search_settings_get_regex_enabled (self->settings))
    g_value_take_string (to_value, gtk_source_utils_escape_search_text (str));
  else
    g_value_set_string (to_value, str);

  return TRUE;
}

static gboolean
mode_to_boolean (GBinding     *binding,
                 const GValue *from_value,
                 GValue       *to_value,
                 gpointer      user_data)
{
  if (g_value_get_enum (from_value) == EDITOR_SEARCH_BAR_MODE_REPLACE)
    g_value_set_boolean (to_value, TRUE);
  else
    g_value_set_boolean (to_value, FALSE);
  return TRUE;
}

static gboolean
boolean_to_mode (GBinding     *binding,
                 const GValue *from_value,
                 GValue       *to_value,
                 gpointer      user_data)
{
  if (g_value_get_boolean (from_value))
    g_value_set_enum (to_value, EDITOR_SEARCH_BAR_MODE_REPLACE);
  else
    g_value_set_enum (to_value, EDITOR_SEARCH_BAR_MODE_SEARCH);
  return TRUE;
}

void
_editor_search_bar_grab_focus (EditorSearchBar *self)
{
  g_return_if_fail (EDITOR_IS_SEARCH_BAR (self));

  gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

static void
on_notify_replace_text_cb (EditorSearchBar *self,
                           GParamSpec      *pspec,
                           GtkEntry        *entry)
{
  g_assert (EDITOR_IS_SEARCH_BAR (self));
  g_assert (GTK_IS_ENTRY (entry));

  update_properties (self);
}

static void
on_notify_search_text_cb (EditorSearchBar   *self,
                          GParamSpec        *pspec,
                          EditorSearchEntry *entry)
{
  g_assert (EDITOR_IS_SEARCH_BAR (self));
  g_assert (EDITOR_IS_SEARCH_ENTRY (entry));

  self->scroll_to_first_match = TRUE;
}

static gboolean
on_search_key_pressed_cb (GtkEventControllerKey *key,
                          guint                  keyval,
                          guint                  keycode,
                          GdkModifierType        state,
                          EditorSearchBar       *self)
{
  g_assert (GTK_IS_EVENT_CONTROLLER_KEY (key));
  g_assert (EDITOR_IS_SEARCH_BAR (self));

  if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) == 0)
    {
      switch (keyval)
        {
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
          _editor_search_bar_move_previous (self, FALSE);
          return TRUE;

        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
          _editor_search_bar_move_next (self, FALSE);
          return TRUE;

        default:
          break;
        }
    }

  return FALSE;
}

static gboolean
editor_search_bar_focus (GtkWidget        *widget,
                         GtkDirectionType  dir)
{
  EditorSearchBar *self = EDITOR_SEARCH_BAR (widget);
  GtkWidget *focus;
  GtkWidget *popover;

  /* If the focus is in the menu popover then use the default
   * movements so we don't ness that up.
   */
  focus = gtk_root_get_focus (gtk_widget_get_root (widget));
  if (focus != NULL &&
      gtk_widget_is_ancestor (focus, widget) &&
      (popover = gtk_widget_get_ancestor (focus, GTK_TYPE_POPOVER)))
    return GTK_WIDGET_CLASS (editor_search_bar_parent_class)->focus (widget, dir);

  if (_editor_focus_chain_focus_child (widget, &self->focus_chain, dir))
    return TRUE;

  return GTK_WIDGET_CLASS (editor_search_bar_parent_class)->focus (widget, dir);
}

static void
editor_search_bar_dispose (GObject *object)
{
  EditorSearchBar *self = (EditorSearchBar *)object;
  GtkWidget *child;

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->context);

  gtk_widget_dispose_template (GTK_WIDGET (self), EDITOR_TYPE_SEARCH_BAR);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  if (self->focus_chain.length > 0)
    g_queue_clear (&self->focus_chain);

  G_OBJECT_CLASS (editor_search_bar_parent_class)->dispose (object);
}

static void
editor_search_bar_finalize (GObject *object)
{
  EditorSearchBar *self = (EditorSearchBar *)object;

  g_clear_object (&self->context);
  g_clear_object (&self->settings);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (editor_search_bar_parent_class)->finalize (object);
}

static void
editor_search_bar_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EditorSearchBar *self = EDITOR_SEARCH_BAR (object);

  switch (prop_id)
    {
    case PROP_MODE:
      g_value_set_enum (value,
                        gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->replace_mode_button)) ?
                        EDITOR_SEARCH_BAR_MODE_REPLACE :
                        EDITOR_SEARCH_BAR_MODE_SEARCH);
      break;

    case PROP_CAN_MOVE:
      g_value_set_boolean (value, _editor_search_bar_get_can_move (self));
      break;

    case PROP_CAN_REPLACE:
      g_value_set_boolean (value, _editor_search_bar_get_can_replace (self));
      break;

    case PROP_CAN_REPLACE_ALL:
      g_value_set_boolean (value, _editor_search_bar_get_can_replace_all (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_search_bar_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  EditorSearchBar *self = EDITOR_SEARCH_BAR (object);

  switch (prop_id)
    {
    case PROP_MODE:
      _editor_search_bar_set_mode (self, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_search_bar_class_init (EditorSearchBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_search_bar_dispose;
  object_class->finalize = editor_search_bar_finalize;
  object_class->get_property = editor_search_bar_get_property;
  object_class->set_property = editor_search_bar_set_property;

  widget_class->root = editor_search_bar_root;
  widget_class->unroot = editor_search_bar_unroot;
  widget_class->focus = editor_search_bar_focus;

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "searchbar");
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-search-bar.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, close_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, grid);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, move_previous);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, move_next);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, options_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, replace_all_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, replace_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, replace_entry);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, replace_mode_button);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchBar, search_entry);
  gtk_widget_class_bind_template_callback (widget_class, on_search_key_pressed_cb);

  signals [MOVE_NEXT_SEARCH] =
    g_signal_new_class_handler ("move-next-search",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_CALLBACK (_editor_search_bar_move_next),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  signals [MOVE_PREVIOUS_SEARCH] =
    g_signal_new_class_handler ("move-previous-search",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_CALLBACK (_editor_search_bar_move_previous),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Escape, 0, "search.cancel", NULL);

  gtk_widget_class_install_action (widget_class, "search.activate", NULL, search_activate);
  gtk_widget_class_install_action (widget_class, "search.cancel", NULL, search_cancel);

  properties [PROP_MODE] =
    g_param_spec_enum ("mode",
                       "Mode",
                       "The mode for the search bar",
                       EDITOR_TYPE_SEARCH_BAR_MODE,
                       EDITOR_SEARCH_BAR_MODE_SEARCH,
                       (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_CAN_MOVE] =
    g_param_spec_boolean ("can-move",
                          "Can Move",
                          "If there are search results",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CAN_REPLACE] =
    g_param_spec_boolean ("can-replace",
                          "Can Replace",
                          "If search is ready to replace a single result",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CAN_REPLACE_ALL] =
    g_param_spec_boolean ("can-replace-all",
                          "Can Replace All",
                          "If search is ready to replace all results",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  g_type_ensure (EDITOR_TYPE_SEARCH_ENTRY);
}

static void
editor_search_bar_init (EditorSearchBar *self)
{
  g_autoptr(GSimpleActionGroup) group = NULL;
  g_autoptr(GPropertyAction) regex_action = NULL;
  g_autoptr(GPropertyAction) case_action = NULL;
  g_autoptr(GPropertyAction) word_action = NULL;

  self->cancellable = g_cancellable_new ();

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Setup focus chain */
  g_queue_push_tail (&self->focus_chain, GTK_WIDGET (self->search_entry));
  g_queue_push_tail (&self->focus_chain, GTK_WIDGET (self->replace_entry));
  g_queue_push_tail (&self->focus_chain, GTK_WIDGET (self->move_previous));
  g_queue_push_tail (&self->focus_chain, GTK_WIDGET (self->move_next));
  g_queue_push_tail (&self->focus_chain, GTK_WIDGET (self->replace_mode_button));
  g_queue_push_tail (&self->focus_chain, GTK_WIDGET (self->options_button));
  g_queue_push_tail (&self->focus_chain, GTK_WIDGET (self->close_button));
  g_queue_push_tail (&self->focus_chain, GTK_WIDGET (self->replace_button));
  g_queue_push_tail (&self->focus_chain, GTK_WIDGET (self->replace_all_button));

  g_signal_connect_object (self->replace_entry,
                           "notify::text",
                           G_CALLBACK (on_notify_replace_text_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->search_entry,
                           "notify::text",
                           G_CALLBACK (on_notify_search_text_cb),
                           self,
                           G_CONNECT_SWAPPED);

  self->settings = gtk_source_search_settings_new ();

  gtk_source_search_settings_set_wrap_around (self->settings, TRUE);

  g_object_bind_property_full (self->settings, "search-text",
                               self->search_entry, "text",
                               G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL,
                               search_text_to_text, text_to_search_text, self, NULL);
  g_object_bind_property_full (self->replace_mode_button, "active",
                               self, "mode",
                               G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL,
                               boolean_to_mode, mode_to_boolean, NULL, NULL);

  regex_action = g_property_action_new ("regex", G_OBJECT (self->settings), "regex-enabled");
  case_action = g_property_action_new ("case-sensitive", G_OBJECT (self->settings), "case-sensitive");
  word_action = g_property_action_new ("match-whole-word", G_OBJECT (self->settings), "at-word-boundaries");

  group = g_simple_action_group_new ();
  g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (regex_action));
  g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (case_action));
  g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (word_action));

  gtk_widget_insert_action_group (GTK_WIDGET (self), "search-options", G_ACTION_GROUP (group));
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
  gtk_toggle_button_set_active (self->replace_mode_button, is_replace);

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_MODE]);
}

static void
scroll_to_first_match (EditorSearchBar        *self,
                       GtkSourceSearchContext *context)
{
  GtkTextIter iter, match_begin, match_end;
  GtkTextBuffer *buffer;
  GtkWidget *page;
  gboolean wrapped;

  g_assert (EDITOR_IS_SEARCH_BAR (self));
  g_assert (GTK_SOURCE_IS_SEARCH_CONTEXT (context));

  if (!(page = gtk_widget_get_ancestor (GTK_WIDGET (self), EDITOR_TYPE_PAGE)))
    return;

  buffer = GTK_TEXT_BUFFER (gtk_source_search_context_get_buffer (context));
  gtk_text_buffer_get_iter_at_offset (buffer, &iter, self->offset_when_shown);
  if (gtk_source_search_context_forward (context, &iter, &match_begin, &match_end, &wrapped))
    {
      gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (EDITOR_PAGE (page)->view),
                                    &match_begin, 0.1, FALSE, 0, 0);
      self->jump_back_on_hide = TRUE;
    }

  self->scroll_to_first_match = FALSE;
}

static void
editor_search_bar_notify_occurrences_count_cb (EditorSearchBar        *self,
                                               GParamSpec             *pspec,
                                               GtkSourceSearchContext *context)
{
  guint occurrence_count;

  g_assert (EDITOR_IS_SEARCH_BAR (self));
  g_assert (GTK_SOURCE_IS_SEARCH_CONTEXT (context));

  occurrence_count = gtk_source_search_context_get_occurrences_count (context);
  editor_search_entry_set_occurrence_count (self->search_entry, occurrence_count);

  if (self->scroll_to_first_match && occurrence_count > 0)
    scroll_to_first_match (self, context);

  if (occurrence_count == 0 &&
      g_strcmp0 (gtk_editable_get_text (GTK_EDITABLE (self->search_entry)), "") != 0)
    gtk_widget_add_css_class (GTK_WIDGET (self->search_entry), "error");
  else
    gtk_widget_remove_css_class (GTK_WIDGET (self->search_entry), "error");

  update_properties (self);
}

static void
editor_search_bar_cursor_moved_cb (EditorSearchBar *self,
                                   EditorDocument  *document)
{
  g_assert (EDITOR_IS_SEARCH_BAR (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  update_properties (self);
}

void
_editor_search_bar_attach (EditorSearchBar *self,
                           EditorDocument  *document)
{
  GtkTextBuffer *buffer = (GtkTextBuffer *)document;
  GtkTextIter begin, end, insert;

  g_return_if_fail (EDITOR_IS_SEARCH_BAR (self));

  _editor_document_save_insert_mark (document);

  gtk_text_buffer_get_iter_at_mark (buffer, &insert, gtk_text_buffer_get_insert (buffer));
  self->offset_when_shown = gtk_text_iter_get_offset (&insert);

  if (gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (document), &begin, &end))
    {
      g_autofree gchar *text = gtk_text_iter_get_slice (&begin, &end);
      gtk_editable_set_text (GTK_EDITABLE (self->search_entry), text);
    }

  if (self->context != NULL)
    return;

  self->context = gtk_source_search_context_new (GTK_SOURCE_BUFFER (document), self->settings);

  g_signal_connect_object (self->context,
                           "notify::occurrences-count",
                           G_CALLBACK (editor_search_bar_notify_occurrences_count_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (document,
                           "cursor-moved",
                           G_CALLBACK (editor_search_bar_cursor_moved_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

void
_editor_search_bar_detach (EditorSearchBar *self)
{
  g_return_if_fail (EDITOR_IS_SEARCH_BAR (self));

  if (self->context != NULL)
    {
      EditorDocument *document = EDITOR_DOCUMENT (gtk_source_search_context_get_buffer (self->context));
      GtkWidget *page = gtk_widget_get_ancestor (GTK_WIDGET (self), EDITOR_TYPE_PAGE);
      GtkTextIter begin, end;

      if (self->jump_back_on_hide)
        _editor_page_scroll_to_insert (EDITOR_PAGE (page));

      gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (document), &begin, &end);
      gtk_text_buffer_remove_tag (GTK_TEXT_BUFFER (document), self->current_tag, &begin, &end);

      g_signal_handlers_disconnect_by_func (self->context,
                                            G_CALLBACK (editor_search_bar_notify_occurrences_count_cb),
                                            self);
      g_signal_handlers_disconnect_by_func (document,
                                            G_CALLBACK (editor_search_bar_cursor_moved_cb),
                                            self);

      g_clear_object (&self->context);
    }

  self->hide_after_move = FALSE;
  self->jump_back_on_hide = FALSE;
}

gboolean
_editor_search_bar_get_can_move (EditorSearchBar *self)
{
  g_return_val_if_fail (EDITOR_IS_SEARCH_BAR (self), FALSE);

  return self->context != NULL &&
         gtk_source_search_context_get_occurrences_count (self->context) > 0;
}

gboolean
_editor_search_bar_get_can_replace (EditorSearchBar *self)
{
  GtkTextIter begin, end;
  GtkTextBuffer *buffer;

  g_return_val_if_fail (EDITOR_IS_SEARCH_BAR (self), FALSE);

  if (self->context == NULL)
    return FALSE;

  buffer = GTK_TEXT_BUFFER (gtk_source_search_context_get_buffer (self->context));

  return _editor_search_bar_get_can_move (self) &&
         gtk_text_buffer_get_selection_bounds (buffer, &begin, &end) &&
         gtk_source_search_context_get_occurrence_position (self->context, &begin, &end) > 0;
}

gboolean
_editor_search_bar_get_can_replace_all (EditorSearchBar *self)
{
  g_return_val_if_fail (EDITOR_IS_SEARCH_BAR (self), FALSE);

  return _editor_search_bar_get_can_move (self);
}

void
_editor_search_bar_replace (EditorSearchBar *self)
{
  g_autoptr(GError) error = NULL;
  GtkSourceBuffer *buffer;
  GtkTextIter begin, end;
  const char *replace;

  g_return_if_fail (EDITOR_IS_SEARCH_BAR (self));

  if (!_editor_search_bar_get_can_replace (self))
    return;

  buffer = gtk_source_search_context_get_buffer (self->context);
  replace = gtk_editable_get_text (GTK_EDITABLE (self->replace_entry));

  gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (buffer), &begin, &end);

  if (!gtk_source_search_context_replace (self->context, &begin, &end, replace, -1, &error))
    {
      g_warning ("Failed to replace match: %s", error->message);
      return;
    }

  gtk_text_buffer_select_range (GTK_TEXT_BUFFER (buffer), &end, &end);
  _editor_search_bar_move_next (self, FALSE);
}

void
_editor_search_bar_replace_all (EditorSearchBar *self)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *unescaped = NULL;
  const char *replace;

  g_return_if_fail (EDITOR_IS_SEARCH_BAR (self));

  if (!_editor_search_bar_get_can_replace_all (self))
    return;

  replace = gtk_editable_get_text (GTK_EDITABLE (self->replace_entry));
  unescaped = gtk_source_utils_unescape_search_text (replace);

  if (!gtk_source_search_context_replace_all (self->context, unescaped, -1, &error))
    g_warning ("Failed to replace all matches: %s", error->message);
}
