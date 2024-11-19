/* editor-source-view.c
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

#include "editor-application-private.h"
#include "editor-document-private.h"
#include "editor-joined-menu-private.h"
#include "editor-page-private.h"
#include "editor-search-bar-private.h"
#include "editor-source-iter.h"
#include "editor-source-view.h"
#include "editor-utils-private.h"

#define MIN_BUBBLE_SCALE -2
#define MAX_BUBBLE_SCALE 3
#define X_PAD 5
#define Y_PAD 3

struct _EditorSourceView
{
  GtkSourceView parent_instance;
  GtkCssProvider *css_provider;
  PangoFontDescription *font_desc;
  GMenuModel *spelling_menu;
  char *spelling_word;
  int font_scale;
  double line_height;
  guint overscroll_source;
};

G_DEFINE_TYPE (EditorSourceView, editor_source_view, GTK_SOURCE_TYPE_VIEW)

enum {
  PROP_0,
  PROP_FONT_DESC,
  PROP_FONT_SCALE,
  PROP_LINE_HEIGHT,
  PROP_ZOOM_LEVEL,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
editor_source_view_update_corrections (EditorSourceView *self)
{
  EditorDocument *document;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  if ((document = EDITOR_DOCUMENT (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self)))))
    editor_document_update_corrections (document);
}

static gboolean
editor_source_view_update_overscroll (gpointer user_data)
{
  EditorSourceView *self = user_data;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  self->overscroll_source = 0;

  if (gtk_widget_get_mapped (GTK_WIDGET (self)))
    {
      GdkRectangle visible_rect;

      gtk_text_view_get_visible_rect (GTK_TEXT_VIEW (self), &visible_rect);
      gtk_text_view_set_bottom_margin (GTK_TEXT_VIEW (self), visible_rect.height * .75);
    }

  return G_SOURCE_REMOVE;
}

static void
editor_source_view_update_css (EditorSourceView *self)
{
  const PangoFontDescription *font_desc;
  PangoFontDescription *scaled = NULL;
  PangoFontDescription *system_font = NULL;
  g_autoptr(GString) str = NULL;
  g_autofree char *font_css = NULL;
  int size = 11; /* 11pt */
  char line_height_str[G_ASCII_DTOSTR_BUF_SIZE];

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  str = g_string_new (NULL);

  g_string_append (str, "textview {\n");

  /* Get font information to adjust line height and font changes */
  if ((font_desc = self->font_desc) == NULL)
    {
      system_font = _editor_application_get_system_font (EDITOR_APPLICATION_DEFAULT);
      font_desc = system_font;
    }

  if (font_desc != NULL &&
      pango_font_description_get_set_fields (font_desc) & PANGO_FONT_MASK_SIZE)
    size = pango_font_description_get_size (font_desc) / PANGO_SCALE;

  if (size + self->font_scale < 1)
    self->font_scale = -size + 1;

  size = MAX (1, size + self->font_scale);

  if (size != 0)
    {
      if (font_desc)
        scaled = pango_font_description_copy (font_desc);
      else
        scaled = pango_font_description_new ();
      pango_font_description_set_size (scaled, size * PANGO_SCALE);
      font_desc = scaled;
    }

  if (font_desc)
    {
      font_css = _editor_font_description_to_css (font_desc);
      g_string_append (str, font_css);
    }

  g_ascii_dtostr (line_height_str, sizeof line_height_str, self->line_height);
  line_height_str[6] = 0;
  g_string_append_printf (str, "\nline-height: %s;\n", line_height_str);

  g_string_append (str, "}\n");

  gtk_css_provider_load_from_string (self->css_provider, str->str);

  g_clear_pointer (&scaled, pango_font_description_free);
  g_clear_pointer (&system_font, pango_font_description_free);
}

static gboolean
on_key_pressed_cb (GtkEventControllerKey *key,
                   guint                  keyval,
                   guint                  keycode,
                   GdkModifierType        state,
                   GtkWidget             *widget)
{
  g_assert (EDITOR_IS_SOURCE_VIEW (widget));

  /* We don't get a callback from GtkTextView when the menu is to be shown
   * via the `menu.popup` action. This tries to discover that before it does
   * and ensure our menu is updated to contain the spelling corrections.
   *
   * See https://gitlab.gnome.org/GNOME/gnome-text-editor/-/issues/693
   */
  if (((state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK && keyval == GDK_KEY_F10) ||
      keyval == GDK_KEY_Menu)
    {
      editor_source_view_update_corrections (EDITOR_SOURCE_VIEW (widget));
      return FALSE;
    }

  /* This seems to be the easiest way to reliably override the keybindings
   * from GtkTextView into something we want (which is to use them for moving
   * through the tabs.
   */

  if ((state & GDK_CONTROL_MASK) == 0)
    return FALSE;

  if ((state & (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) == 0)
    return FALSE;

  switch (keyval)
    {
    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:
      if (state & GDK_SHIFT_MASK)
        gtk_widget_activate_action (widget, "page.move-left", NULL);
      else
        gtk_widget_activate_action (widget, "win.focus-neighbor", "i", -1);
      return TRUE;

    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down:
      if (state & GDK_SHIFT_MASK)
        gtk_widget_activate_action (widget, "page.move-right", NULL);
      else
        gtk_widget_activate_action (widget, "win.focus-neighbor", "i", 1);
      return TRUE;

    default:
      break;
    }

  return FALSE;
}

static void
tweak_gutter_spacing (GtkSourceView *view)
{
  GtkSourceGutter *gutter;
  GtkWidget *child;
  guint n = 0;

  g_assert (GTK_SOURCE_IS_VIEW (view));

  /* Ensure we have a line gutter renderer to tweak */
  gutter = gtk_source_view_get_gutter (view, GTK_TEXT_WINDOW_LEFT);
  gtk_source_view_set_show_line_numbers (view, TRUE);

  /* Add margin to first gutter renderer */
  for (child = gtk_widget_get_first_child (GTK_WIDGET (gutter));
       child != NULL;
       child = gtk_widget_get_next_sibling (child), n++)
    {
      if (GTK_SOURCE_IS_GUTTER_RENDERER (child))
        gtk_widget_set_margin_start (child, n == 0 ? 4 : 0);
    }
}

static void
on_click_pressed_cb (GtkGestureClick  *click,
                     int               n_press,
                     double            x,
                     double            y,
                     EditorSourceView *self)
{
  GdkEventSequence *sequence;
  GtkTextBuffer *buffer;
  GtkTextIter iter, begin, end;
  GdkEvent *event;
  int buf_x, buf_y;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));
  g_assert (GTK_IS_GESTURE_CLICK (click));

  sequence = gtk_gesture_single_get_current_sequence (GTK_GESTURE_SINGLE (click));
  event = gtk_gesture_get_last_event (GTK_GESTURE (click), sequence);

  if (n_press != 1 || !gdk_event_triggers_context_menu (event))
    {
      editor_source_view_update_corrections (self);
      return;
    }

  /* Move the cursor position to where the click occurred so that
   * the context menu will be useful for the click location.
   */
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));
  if (gtk_text_buffer_get_selection_bounds (buffer, &begin, &end))
    {
      editor_source_view_update_corrections (self);
      return;
    }

  gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (self),
                                         GTK_TEXT_WINDOW_WIDGET,
                                         x, y, &buf_x, &buf_y);
  gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (self), &iter, buf_x, buf_y);
  gtk_text_buffer_select_range (buffer, &iter, &iter);

  editor_source_view_update_corrections (self);
}

static void
editor_source_view_zoom (EditorSourceView *self,
                         int               amount)
{
  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  if (amount == 0)
    self->font_scale = 0;
  else
    self->font_scale += amount;

  editor_source_view_update_css (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_FONT_SCALE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM_LEVEL]);
}

static gboolean
on_scroll_scrolled_cb (GtkEventControllerScroll *scroll,
                       double                    dx,
                       double                    dy,
                       EditorSourceView         *self)
{
  GdkModifierType mods;

  g_assert (GTK_IS_EVENT_CONTROLLER_SCROLL (scroll));
  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  mods = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (scroll));

  if ((mods & GDK_CONTROL_MASK) != 0)
    {
      editor_source_view_zoom (self, dy < 0 ? 1 : -1);
      return TRUE;
    }

  return FALSE;
}

static void
on_scroll_begin_cb (GtkEventControllerScroll *scroll,
                    EditorSourceView         *self)
{
  GdkModifierType state;

  g_assert (GTK_IS_EVENT_CONTROLLER_SCROLL (scroll));
  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (scroll));

  if (state & GDK_CONTROL_MASK)
    gtk_event_controller_scroll_set_flags (scroll,
                                           GTK_EVENT_CONTROLLER_SCROLL_VERTICAL |
                                           GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
}

static void
on_scroll_end_cb (GtkEventControllerScroll *scroll,
                  EditorSourceView         *self)
{
  g_assert (GTK_IS_EVENT_CONTROLLER_SCROLL (scroll));
  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  gtk_event_controller_scroll_set_flags (scroll, GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
}

static void
on_notify_buffer_cb (EditorSourceView *self,
                     GParamSpec       *pspec,
                     gpointer          unused)
{
  GtkTextBuffer *buffer;
  GMenuModel *model;
  GMenuModel *extra_menu;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));
  if (!EDITOR_IS_DOCUMENT (buffer))
    return;

  g_signal_connect_object (buffer,
                           "notify::style-scheme",
                           G_CALLBACK (editor_source_view_update_css),
                           self,
                           G_CONNECT_SWAPPED);

  g_object_bind_property (buffer, "loading", self, "editable",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);

  _editor_document_attach_actions (EDITOR_DOCUMENT (buffer), GTK_WIDGET (self));

  model = editor_document_get_spelling_menu (EDITOR_DOCUMENT (buffer));
  extra_menu = gtk_text_view_get_extra_menu (GTK_TEXT_VIEW (self));

  g_assert (model != NULL);
  g_assert (EDITOR_IS_JOINED_MENU (extra_menu));

  editor_joined_menu_append_menu (EDITOR_JOINED_MENU (extra_menu), model);
}

static void
editor_source_view_action_zoom (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *param)
{
  EditorSourceView *self = (EditorSourceView *)widget;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  if (g_strcmp0 (action_name, "page.zoom-in") == 0)
    editor_source_view_zoom (self, 1);
  else if (g_strcmp0 (action_name, "page.zoom-out") == 0)
    editor_source_view_zoom (self, -1);
  else if (g_strcmp0 (action_name, "page.zoom-one") == 0)
    editor_source_view_zoom (self, 0);
  else
    g_assert_not_reached ();
}

static void
select_line (GtkTextBuffer *buffer,
             GtkTextIter   *begin,
             GtkTextIter   *end)
{
  gboolean had_selection;

  g_assert (GTK_IS_TEXT_BUFFER (buffer));

  had_selection = gtk_text_buffer_get_selection_bounds (buffer, begin, end);
  gtk_text_iter_order (begin, end);

  /* Leave existing line-wise selections in place, such as those
   * from using ctrl+l to select line.
   */
  if (had_selection &&
      gtk_text_iter_starts_line (begin) &&
      gtk_text_iter_starts_line (end) &&
      gtk_text_iter_get_line (begin) != gtk_text_iter_get_line (end))
    return;

  /* Move beginning of selection/cursor to position 0 of first */
  if (!gtk_text_iter_starts_line (begin))
    gtk_text_iter_set_line_offset (begin, 0);

  /* Move end of selection/cursor to end of line */
  if (!gtk_text_iter_ends_line (end))
    gtk_text_iter_forward_to_line_end (end);

  /* Swallow the \n with the selection */
  if (!gtk_text_iter_is_end (end))
    gtk_text_iter_forward_line (end);
}

static void
editor_source_view_action_select_line (GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *param)
{
  EditorSourceView *self = (EditorSourceView *)widget;
  GtkTextBuffer *buffer;
  GtkTextIter begin, end;
  GtkTextIter cur_begin, cur_end;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));

  select_line (buffer, &begin, &end);

  /* If our guessed selection matches what we already have selected,
   * then we want to extend the selection one more line.
   */
  if (gtk_text_buffer_get_selection_bounds (buffer, &cur_begin, &cur_end))
    {
      gtk_text_iter_order (&cur_begin, &cur_end);
      if (gtk_text_iter_equal (&cur_begin, &begin) &&
          gtk_text_iter_equal (&cur_end, &end))
        gtk_text_iter_forward_line (&end);
    }

  gtk_text_buffer_select_range (buffer, &begin, &end);

  /* NOTE: This shouldn't be needed, but due to some invalidation issues in
   *       the line display cache, seems to improve chances we get proper
   *       invalidation lines within cache.
   */
  gtk_widget_queue_draw (widget);
}

static void
editor_source_view_action_delete_line (GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *param)
{
  EditorSourceView *self = (EditorSourceView *)widget;
  GtkTextBuffer *buffer;
  GtkTextIter begin, end;
  g_autofree char *text = NULL;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));

  /* If we're at the end of the buffer, then we need to remove the
   * leading \n instead of the trailing \n to make it appear to the
   * user that a line was removed.
   */
  gtk_text_buffer_begin_user_action (buffer);
  select_line (buffer, &begin, &end);
  gtk_text_iter_order (&begin, &end);
  if (gtk_text_iter_is_end (&end) &&
      gtk_text_iter_get_line (&begin) == gtk_text_iter_get_line (&end))
    gtk_text_iter_backward_char (&begin);
  text = gtk_text_iter_get_slice (&begin, &end);
  gtk_text_buffer_delete (buffer, &begin, &end);
  gtk_text_buffer_end_user_action (buffer);

  /* now move the cursor to the beginning of the new line */
  gtk_text_iter_set_line_offset (&begin, 0);
  while (!gtk_text_iter_ends_line (&begin) &&
         g_unichar_isspace (gtk_text_iter_get_char (&begin)))
    gtk_text_iter_forward_char (&begin);
  gtk_text_buffer_select_range (buffer, &begin, &begin);

  /* it's nice to place the text into the primary selection so that
   * the user can paste it in other places.
   */
  gdk_clipboard_set_text (gtk_widget_get_primary_clipboard (widget), text);
}

static void
editor_source_view_action_duplicate_line (GtkWidget  *widget,
                                          const char *action_name,
                                          GVariant   *param)
{
  g_autofree char *text = NULL;
  g_autofree char *duplicate_line = NULL;
  GtkTextIter begin, end;
  GtkTextMark *cursor;
  GtkTextBuffer *buffer;
  gboolean selected;

  g_assert (EDITOR_IS_SOURCE_VIEW (widget));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));
  cursor = gtk_text_buffer_get_insert (buffer);

  gtk_text_buffer_begin_user_action (buffer);

  selected = gtk_text_buffer_get_selection_bounds (buffer, &begin, &end);

  if (selected)
    {
      duplicate_line = gtk_text_iter_get_text (&begin, &end);
      gtk_text_buffer_insert (buffer, &begin, duplicate_line, -1);
    }
  else
    {
      gtk_text_buffer_get_iter_at_mark (buffer, &begin, cursor);
      end = begin;

      gtk_text_iter_set_line_offset (&begin, 0);

      if (!gtk_text_iter_ends_line (&end))
        gtk_text_iter_forward_to_line_end (&end);

      if (gtk_text_iter_get_line (&begin) == gtk_text_iter_get_line (&end))
        {
          text = gtk_text_iter_get_text (&begin, &end);
          duplicate_line = g_strconcat (text, "\n", NULL);
          gtk_text_buffer_insert (buffer, &begin, duplicate_line, -1);
        }
    }

  gtk_text_buffer_end_user_action (buffer);
}

static gboolean
editor_source_view_scroll_to_insert_in_idle_cb (gpointer user_data)
{
  EditorSourceView *self = user_data;
  GtkTextBuffer *buffer;
  GtkTextMark *mark;
  GtkTextIter iter;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));
  mark = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, mark);

  editor_source_view_jump_to_iter (GTK_TEXT_VIEW (self), &iter, .25, TRUE, 1.0, 0.5);

  return G_SOURCE_REMOVE;
}

static void
editor_source_view_root (GtkWidget *widget)
{
  EditorSourceView *self = (EditorSourceView *)widget;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  GTK_WIDGET_CLASS (editor_source_view_parent_class)->root (widget);

  g_idle_add_full (G_PRIORITY_LOW,
                   editor_source_view_scroll_to_insert_in_idle_cb,
                   g_object_ref (self),
                   g_object_unref);
}

static void
editor_source_view_size_allocate (GtkWidget *widget,
                                  int        width,
                                  int        height,
                                  int        baseline)
{
  EditorSourceView *self = (EditorSourceView *)widget;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  GTK_WIDGET_CLASS (editor_source_view_parent_class)->size_allocate (widget, width, height, baseline);

  if (self->overscroll_source == 0)
    self->overscroll_source = g_idle_add (editor_source_view_update_overscroll, self);
}

static gboolean
editor_source_view_extend_selection (GtkTextView            *text_view,
                                     GtkTextExtendSelection  granularity,
                                     const GtkTextIter      *location,
                                     GtkTextIter            *start,
                                     GtkTextIter            *end)
{
  g_assert (EDITOR_IS_SOURCE_VIEW (text_view));

  if (granularity == GTK_TEXT_EXTEND_SELECTION_WORD)
    {
      /* Use a custom handler to extend selection so that we do not
       * hit potential slow paths in GtkSourceView which uses visibility
       * oriented API. We do not have invisible text and it can be
       * extremely slow with things like minified JSON.
       *
       * See: #742
       */
      _editor_source_iter_extend_selection_word (location, start, end);
      return GDK_EVENT_STOP;
    }

  return GTK_TEXT_VIEW_CLASS (editor_source_view_parent_class)->
      extend_selection (text_view, granularity, location, start, end);
}

static void
editor_source_view_constructed (GObject *object)
{
  EditorSourceView *self = (EditorSourceView *)object;

  G_OBJECT_CLASS (editor_source_view_parent_class)->constructed (object);

  editor_source_view_update_css (self);
}

static void
editor_source_view_dispose (GObject *object)
{
  EditorSourceView *self = (EditorSourceView *)object;

  g_clear_handle_id (&self->overscroll_source, g_source_remove);

  gtk_text_view_set_extra_menu (GTK_TEXT_VIEW (self), NULL);
  gtk_widget_insert_action_group (GTK_WIDGET (self), "spelling", NULL);

  g_clear_object (&self->css_provider);
  g_clear_object (&self->spelling_menu);
  g_clear_pointer (&self->spelling_word, g_free);

  G_OBJECT_CLASS (editor_source_view_parent_class)->dispose (object);
}

static void
editor_source_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  EditorSourceView *self = EDITOR_SOURCE_VIEW (object);

  switch (prop_id)
    {
    case PROP_FONT_DESC:
      g_value_set_boxed (value, editor_source_view_get_font_desc (self));
      break;

    case PROP_FONT_SCALE:
      g_value_set_int (value, self->font_scale);
      break;

    case PROP_LINE_HEIGHT:
      g_value_set_double (value, self->line_height);
      break;

    case PROP_ZOOM_LEVEL:
      g_value_set_double (value, editor_source_view_get_zoom_level (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_source_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  EditorSourceView *self = EDITOR_SOURCE_VIEW (object);

  switch (prop_id)
    {
    case PROP_FONT_DESC:
      editor_source_view_set_font_desc (self, g_value_get_boxed (value));
      break;

    case PROP_FONT_SCALE:
      self->font_scale = g_value_get_int (value);
      editor_source_view_update_css (self);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM_LEVEL]);
      break;

    case PROP_LINE_HEIGHT:
      self->line_height = g_value_get_double (value);
      editor_source_view_update_css (self);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_source_view_class_init (EditorSourceViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkTextViewClass *text_view_class = GTK_TEXT_VIEW_CLASS (klass);

  object_class->constructed = editor_source_view_constructed;
  object_class->dispose = editor_source_view_dispose;
  object_class->get_property = editor_source_view_get_property;
  object_class->set_property = editor_source_view_set_property;

  widget_class->root = editor_source_view_root;
  widget_class->size_allocate = editor_source_view_size_allocate;

  text_view_class->extend_selection = editor_source_view_extend_selection;

  properties [PROP_LINE_HEIGHT] =
    g_param_spec_double ("line-height",
                         "Line height",
                         "The line height of all lines",
                         0.5, 10.0, 1.2,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FONT_DESC] =
    g_param_spec_boxed ("font-desc",
                         "Font Description",
                         "The font to use for text within the editor",
                         PANGO_TYPE_FONT_DESCRIPTION,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FONT_SCALE] =
    g_param_spec_int ("font-scale",
                      "Font Scale",
                      "The font scale",
                      G_MININT, G_MAXINT, 0,
                      (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ZOOM_LEVEL] =
    g_param_spec_double ("zoom-level",
                         "Zoom Level",
                         "Zoom Level",
                         -G_MAXDOUBLE, G_MAXDOUBLE, 1.0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_install_action (widget_class, "page.zoom-in", NULL, editor_source_view_action_zoom);
  gtk_widget_class_install_action (widget_class, "page.zoom-out", NULL, editor_source_view_action_zoom);
  gtk_widget_class_install_action (widget_class, "page.zoom-one", NULL, editor_source_view_action_zoom);
  gtk_widget_class_install_action (widget_class, "buffer.select-line", NULL, editor_source_view_action_select_line);
  gtk_widget_class_install_action (widget_class, "buffer.delete-line", NULL, editor_source_view_action_delete_line);
  gtk_widget_class_install_action (widget_class, "buffer.duplicate-line", NULL, editor_source_view_action_duplicate_line);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_plus, GDK_CONTROL_MASK, "page.zoom-in", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_KP_Add, GDK_CONTROL_MASK, "page.zoom-in", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_equal, GDK_CONTROL_MASK, "page.zoom-in", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_minus, GDK_CONTROL_MASK, "page.zoom-out", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_KP_Subtract, GDK_CONTROL_MASK, "page.zoom-out", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_0, GDK_CONTROL_MASK, "page.zoom-one", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_l, GDK_CONTROL_MASK, "buffer.select-line", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_d, GDK_CONTROL_MASK, "buffer.delete-line", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_d, GDK_CONTROL_MASK|GDK_ALT_MASK, "buffer.duplicate-line", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_g, GDK_CONTROL_MASK, "search.move-next", "b", FALSE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_g, GDK_CONTROL_MASK|GDK_SHIFT_MASK, "search.move-previous", "b", FALSE);
}

static void
editor_source_view_init (EditorSourceView *self)
{
  g_autoptr(EditorJoinedMenu) joined = NULL;
  g_autoptr(GMenu) gsv_section = NULL;
  GtkEventController *controller;
  GMenuModel *extra_menu;

  g_signal_connect_object (EDITOR_APPLICATION_DEFAULT,
                           "notify::system-font-name",
                           G_CALLBACK (editor_source_view_update_css),
                           self,
                           G_CONNECT_SWAPPED);

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    {
      GtkStyleContext *style_context = gtk_widget_get_style_context (GTK_WIDGET (self));

      self->css_provider = gtk_css_provider_new ();
      gtk_style_context_add_provider (style_context,
                                      GTK_STYLE_PROVIDER (self->css_provider),
                                      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
  G_GNUC_END_IGNORE_DEPRECATIONS

  g_signal_connect (self,
                    "notify::buffer",
                    G_CALLBACK (on_notify_buffer_cb),
                    NULL);

  controller = gtk_event_controller_key_new ();
  g_signal_connect (controller,
                    "key-pressed",
                    G_CALLBACK (on_key_pressed_cb),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);

  controller = GTK_EVENT_CONTROLLER (gtk_gesture_click_new ());
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (controller), 0);
  g_signal_connect (controller,
                    "pressed",
                    G_CALLBACK (on_click_pressed_cb),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);

  controller = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
  g_signal_connect (controller,
                    "scroll",
                    G_CALLBACK (on_scroll_scrolled_cb),
                    self);
  g_signal_connect (controller,
                    "scroll-begin",
                    G_CALLBACK (on_scroll_begin_cb),
                    self);
  g_signal_connect (controller,
                    "scroll-end",
                    G_CALLBACK (on_scroll_end_cb),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);

  tweak_gutter_spacing (GTK_SOURCE_VIEW (self));

  joined = editor_joined_menu_new ();
  gsv_section = g_menu_new ();

  extra_menu = gtk_text_view_get_extra_menu (GTK_TEXT_VIEW (self));
  g_menu_append_section (gsv_section, NULL, extra_menu);
  editor_joined_menu_append_menu (joined, G_MENU_MODEL (gsv_section));

  gtk_text_view_set_extra_menu (GTK_TEXT_VIEW (self), G_MENU_MODEL (joined));
}

const PangoFontDescription *
editor_source_view_get_font_desc (EditorSourceView *self)
{
  g_return_val_if_fail (EDITOR_IS_SOURCE_VIEW (self), NULL);

  return self->font_desc;
}

void
editor_source_view_set_font_desc (EditorSourceView           *self,
                                  const PangoFontDescription *font_desc)
{
  g_return_if_fail (EDITOR_IS_SOURCE_VIEW (self));

  if (self->font_desc == font_desc ||
      (self->font_desc != NULL && font_desc != NULL &&
       pango_font_description_equal (self->font_desc, font_desc)))
    return;

  g_clear_pointer (&self->font_desc, pango_font_description_free);

  if (font_desc)
    self->font_desc = pango_font_description_copy (font_desc);

  self->font_scale = 0;

  editor_source_view_update_css (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_FONT_DESC]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_FONT_SCALE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM_LEVEL]);
}

double
editor_source_view_get_zoom_level (EditorSourceView *self)
{
  int alt_size;
  int size = 11; /* 11pt */

  g_return_val_if_fail (EDITOR_IS_SOURCE_VIEW (self), 0);

  if (self->font_desc != NULL &&
      pango_font_description_get_set_fields (self->font_desc) & PANGO_FONT_MASK_SIZE)
    size = pango_font_description_get_size (self->font_desc) / PANGO_SCALE;

  alt_size = MAX (1, size + self->font_scale);

  return (double)alt_size / (double)size;
}

void
editor_source_view_prepend_extra_menu (EditorSourceView *self,
                                       GMenuModel       *extra_menu)
{
  GMenuModel *base;

  g_return_if_fail (EDITOR_IS_SOURCE_VIEW (self));
  g_return_if_fail (G_IS_MENU_MODEL (extra_menu));

  base = gtk_text_view_get_extra_menu (GTK_TEXT_VIEW (self));

  editor_joined_menu_prepend_menu (EDITOR_JOINED_MENU (base), extra_menu);
}

/**
 * editor_source_view_jump_to_iter:
 *
 * The goal of this function is to be like gtk_text_view_scroll_to_iter() but
 * without any of the scrolling animation. We use it to move to a position
 * when animations would cause additional distractions.
 */
void
editor_source_view_jump_to_iter (GtkTextView       *text_view,
                                 const GtkTextIter *iter,
                                 double             within_margin,
                                 gboolean           use_align,
                                 double             xalign,
                                 double             yalign)
{
  GtkAdjustment *hadj;
  GtkAdjustment *vadj;
  GdkRectangle rect;
  GdkRectangle screen;
  int xvalue = 0;
  int yvalue = 0;
  int scroll_dest;
  int screen_bottom;
  int screen_right;
  int screen_xoffset;
  int screen_yoffset;
  int current_x_scroll;
  int current_y_scroll;
  int top_margin;

  /*
   * Many parts of this function were taken from gtk_text_view_scroll_to_iter ()
   * https://developer.gnome.org/gtk3/stable/GtkTextView.html#gtk-text-view-scroll-to-iter
   */

  g_return_if_fail (GTK_IS_TEXT_VIEW (text_view));
  g_return_if_fail (iter != NULL);
  g_return_if_fail (within_margin >= 0.0 && within_margin <= 0.5);
  g_return_if_fail (xalign >= 0.0 && xalign <= 1.0);
  g_return_if_fail (yalign >= 0.0 && yalign <= 1.0);

  g_object_get (text_view, "top-margin", &top_margin, NULL);

  hadj = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (text_view));
  vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (text_view));

  gtk_text_view_get_iter_location (text_view, iter, &rect);
  gtk_text_view_get_visible_rect (text_view, &screen);

  current_x_scroll = screen.x;
  current_y_scroll = screen.y;

  screen_xoffset = screen.width * within_margin;
  screen_yoffset = screen.height * within_margin;

  screen.x += screen_xoffset;
  screen.y += screen_yoffset;
  screen.width -= screen_xoffset * 2;
  screen.height -= screen_yoffset * 2;


  /* paranoia check */
  if (screen.width < 1)
    screen.width = 1;
  if (screen.height < 1)
    screen.height = 1;

  /* The -1 here ensures that we leave enough space to draw the cursor
   * when this function is used for horizontal scrolling.
   */
  screen_right = screen.x + screen.width - 1;
  screen_bottom = screen.y + screen.height;


  /* The alignment affects the point in the target character that we
   * choose to align. If we're doing right/bottom alignment, we align
   * the right/bottom edge of the character the mark is at; if we're
   * doing left/top we align the left/top edge of the character; if
   * we're doing center alignment we align the center of the
   * character.
   */

  /* Vertical alignment */
  scroll_dest = current_y_scroll;
  if (use_align)
    {
      scroll_dest = rect.y + (rect.height * yalign) - (screen.height * yalign);

      /* if scroll_dest < screen.y, we move a negative increment (up),
       * else a positive increment (down)
       */
      yvalue = scroll_dest - screen.y + screen_yoffset;
    }
  else
    {
      /* move minimum to get onscreen */
      if (rect.y < screen.y)
        {
          scroll_dest = rect.y;
          yvalue = scroll_dest - screen.y - screen_yoffset;
        }
      else if ((rect.y + rect.height) > screen_bottom)
        {
          scroll_dest = rect.y + rect.height;
          yvalue = scroll_dest - screen_bottom + screen_yoffset;
        }
    }
  yvalue += current_y_scroll;

  /* Horizontal alignment */
  scroll_dest = current_x_scroll;
  if (use_align)
    {
      scroll_dest = rect.x + (rect.width * xalign) - (screen.width * xalign);

      /* if scroll_dest < screen.y, we move a negative increment (left),
       * else a positive increment (right)
       */
      xvalue = scroll_dest - screen.x + screen_xoffset;
    }
  else
    {
      /* move minimum to get onscreen */
      if (rect.x < screen.x)
        {
          scroll_dest = rect.x;
          xvalue = scroll_dest - screen.x - screen_xoffset;
        }
      else if ((rect.x + rect.width) > screen_right)
        {
          scroll_dest = rect.x + rect.width;
          xvalue = scroll_dest - screen_right + screen_xoffset;
        }
    }
  xvalue += current_x_scroll;

  gtk_adjustment_set_value (hadj, xvalue);
  gtk_adjustment_set_value (vadj, yvalue + top_margin);
}
