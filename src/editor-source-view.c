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

#include "editor-document-private.h"
#include "editor-source-view.h"
#include "editor-joined-menu-private.h"
#include "editor-spell-menu.h"
#include "editor-utils-private.h"

struct _EditorSourceView
{
  GtkSourceView parent_instance;
  GtkCssProvider *css_provider;
  PangoFontDescription *font_desc;
  GMenuModel *spelling_menu;
  char *spelling_word;
  int font_scale;
};

G_DEFINE_TYPE (EditorSourceView, editor_source_view, GTK_SOURCE_TYPE_VIEW)

enum {
  PROP_0,
  PROP_FONT_DESC,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
editor_source_view_update_css (EditorSourceView *self)
{
  const PangoFontDescription *font_desc;
  PangoFontDescription *scaled = NULL;
  g_autoptr(GString) str = NULL;
  g_autofree char *font_css = NULL;
  int size = 11; /* 11pt */

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  if (self->font_desc != NULL &&
      pango_font_description_get_set_fields (self->font_desc) & PANGO_FONT_MASK_SIZE)
    size = pango_font_description_get_size (self->font_desc) / PANGO_SCALE;

  size = MAX (1, size + self->font_scale);

  font_desc = self->font_desc;

  if (size != 0)
    {
      if (font_desc)
        scaled = pango_font_description_copy (font_desc);
      else
        scaled = pango_font_description_new ();
      pango_font_description_set_size (scaled, size * PANGO_SCALE);
      font_desc = scaled;
    }

  str = g_string_new ("textview {\n");
  if (font_desc)
    {

      font_css = _editor_font_description_to_css (font_desc);
      g_string_append (str, font_css);
      g_string_append (str, "\nline-height:1.2;\n");
    }
  g_string_append (str, "}\n");

  gtk_css_provider_load_from_data (self->css_provider, str->str, -1);

  g_clear_pointer (&scaled, pango_font_description_free);
}

static gboolean
on_key_pressed_cb (GtkEventControllerKey *key,
                   guint                  keyval,
                   guint                  keycode,
                   GdkModifierType        state,
                   GtkWidget             *widget)
{
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
  g_auto(GStrv) corrections = NULL;
  g_autofree char *word = NULL;
  GtkTextBuffer *buffer;
  GdkEvent *event;
  GtkTextIter iter, begin, end;
  int buf_x, buf_y;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));
  g_assert (GTK_IS_GESTURE_CLICK (click));

  sequence = gtk_gesture_single_get_current_sequence (GTK_GESTURE_SINGLE (click));
  event = gtk_gesture_get_last_event (GTK_GESTURE (click), sequence);

  if (n_press != 1 || !gdk_event_triggers_context_menu (event))
    goto cleanup;

  /* Move the cursor position to where the click occurred so that
   * the context menu will be useful for the click location.
   */
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));
  if (gtk_text_buffer_get_selection_bounds (buffer, &begin, &end))
    goto cleanup;

  gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (self),
                                         GTK_TEXT_WINDOW_WIDGET,
                                         x, y, &buf_x, &buf_y);
  gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (self), &iter, buf_x, buf_y);
  gtk_text_buffer_select_range (buffer, &iter, &iter);

  /* Get the word under the cursor */
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, gtk_text_buffer_get_insert (buffer));
  begin = iter;
  if (!gtk_text_iter_starts_word (&begin))
    gtk_text_iter_backward_word_start (&begin);
  end = begin;
  if (!gtk_text_iter_ends_word (&end))
    gtk_text_iter_forward_word_end (&end);
  if (!gtk_text_iter_equal (&begin, &end) &&
      gtk_text_iter_compare (&begin, &iter) <= 0 &&
      gtk_text_iter_compare (&iter, &end) <= 0)
    {
      word = gtk_text_iter_get_slice (&begin, &end);

      if (!_editor_document_check_spelling (EDITOR_DOCUMENT (buffer), word))
        corrections = _editor_document_list_corrections (EDITOR_DOCUMENT (buffer), word);
      else
        g_clear_pointer (&word, g_free);
    }

cleanup:
  g_free (self->spelling_word);
  self->spelling_word = g_steal_pointer (&word);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "spelling.add", self->spelling_word != NULL);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "spelling.ignore", self->spelling_word != NULL);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "spelling.correct", self->spelling_word != NULL);
  editor_spell_menu_set_corrections (self->spelling_menu,
                                     (const char * const *)corrections);
}

static void
on_notify_buffer_cb (EditorSourceView *self,
                     GParamSpec       *pspec,
                     gpointer          unused)
{
  GtkTextBuffer *buffer;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));

  if (EDITOR_IS_DOCUMENT (buffer))
    _editor_document_attach_actions (EDITOR_DOCUMENT (buffer), GTK_WIDGET (self));
}

static void
editor_source_view_action_spelling_add (GtkWidget  *widget,
                                        const char *action_name,
                                        GVariant   *param)
{
  EditorSourceView *self = (EditorSourceView *)widget;
  GtkTextBuffer *buffer;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));

  if (EDITOR_IS_DOCUMENT (buffer))
    {
      g_debug ("Adding “%s” to dictionary\n", self->spelling_word);
      _editor_document_add_spelling (EDITOR_DOCUMENT (buffer), self->spelling_word);
    }
}

static void
editor_source_view_action_spelling_ignore (GtkWidget  *widget,
                                           const char *action_name,
                                           GVariant   *param)
{
  EditorSourceView *self = (EditorSourceView *)widget;
  GtkTextBuffer *buffer;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));

  if (EDITOR_IS_DOCUMENT (buffer))
    {
      g_debug ("Ignoring “%s”\n", self->spelling_word);
      _editor_document_ignore_spelling (EDITOR_DOCUMENT (buffer), self->spelling_word);
    }
}

static void
editor_source_view_action_spelling_correct (GtkWidget  *widget,
                                            const char *action_name,
                                            GVariant   *param)
{
  EditorSourceView *self = (EditorSourceView *)widget;
  g_autofree char *slice = NULL;
  GtkTextBuffer *buffer;
  const char *word;
  GtkTextIter begin, end;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_STRING));
  g_assert (self->spelling_word != NULL);

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));
  word = g_variant_get_string (param, NULL);

  if (!EDITOR_IS_DOCUMENT (buffer))
    return;

  /* We don't deal with selections (yet?) */
  if (gtk_text_buffer_get_selection_bounds (buffer, &begin, &end))
    return;

  if (!gtk_text_iter_starts_word (&begin))
    gtk_text_iter_backward_word_start (&begin);

  if (!gtk_text_iter_ends_word (&end))
    gtk_text_iter_forward_word_end (&end);

  slice = gtk_text_iter_get_slice (&begin, &end);

  if (g_strcmp0 (slice, self->spelling_word) != 0)
    {
      g_debug ("Words do not match, will not replace.");
      return;
    }

  gtk_text_buffer_begin_user_action (buffer);
  gtk_text_buffer_delete (buffer, &begin, &end);
  gtk_text_buffer_insert (buffer, &begin, word, -1);
  gtk_text_buffer_end_user_action (buffer);
}

static void
editor_source_view_action_zoom (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *param)
{
  EditorSourceView *self = (EditorSourceView *)widget;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  if (g_strcmp0 (action_name, "page.zoom-in") == 0)
    self->font_scale++;
  else if (g_strcmp0 (action_name, "page.zoom-out") == 0)
    self->font_scale--;
  else if (g_strcmp0 (action_name, "page.zoom-one") == 0)
    self->font_scale = 0;
  else
    g_assert_not_reached ();

  editor_source_view_update_css (self);
}

static void
editor_source_view_action_select_line (GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *param)
{
  EditorSourceView *self = (EditorSourceView *)widget;
  GtkTextBuffer *buffer;
  GtkTextIter begin, end;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));
  gtk_text_buffer_get_selection_bounds (buffer, &begin, &end);
  gtk_text_iter_order (&begin, &end);

  /* Move beginning of selection/cursor to position 0 of first */
  if (!gtk_text_iter_starts_line (&begin))
    gtk_text_iter_set_line_offset (&begin, 0);

  /* Move end of selection/cursor to end of line */
  if (!gtk_text_iter_ends_line (&end))
    gtk_text_iter_forward_to_line_end (&end);

  /* Swallow the \n with the selection */
  if (!gtk_text_iter_is_end (&end))
    gtk_text_iter_forward_char (&end);

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

  editor_source_view_action_select_line (widget, NULL, NULL);
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));

  /* If we're at the end of the buffer, then we need to remove the
   * leading \n instead of the trailing \n to make it appear to the
   * user that a line was removed.
   */
  gtk_text_buffer_begin_user_action (buffer);
  gtk_text_buffer_get_selection_bounds (buffer, &begin, &end);
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

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_source_view_class_init (EditorSourceViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = editor_source_view_constructed;
  object_class->dispose = editor_source_view_dispose;
  object_class->get_property = editor_source_view_get_property;
  object_class->set_property = editor_source_view_set_property;

  properties [PROP_FONT_DESC] =
    g_param_spec_boxed ("font-desc",
                         "Font Description",
                         "The font to use for text within the editor",
                         PANGO_TYPE_FONT_DESCRIPTION,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_install_action (widget_class, "page.zoom-in", NULL, editor_source_view_action_zoom);
  gtk_widget_class_install_action (widget_class, "page.zoom-out", NULL, editor_source_view_action_zoom);
  gtk_widget_class_install_action (widget_class, "page.zoom-one", NULL, editor_source_view_action_zoom);
  gtk_widget_class_install_action (widget_class, "spelling.add", NULL, editor_source_view_action_spelling_add);
  gtk_widget_class_install_action (widget_class, "spelling.ignore", NULL, editor_source_view_action_spelling_ignore);
  gtk_widget_class_install_action (widget_class, "spelling.correct", "s", editor_source_view_action_spelling_correct);
  gtk_widget_class_install_action (widget_class, "buffer.select-line", NULL, editor_source_view_action_select_line);
  gtk_widget_class_install_action (widget_class, "buffer.delete-line", NULL, editor_source_view_action_delete_line);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_plus, GDK_CONTROL_MASK, "page.zoom-in", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_KP_Add, GDK_CONTROL_MASK, "page.zoom-in", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_minus, GDK_CONTROL_MASK, "page.zoom-out", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_KP_Subtract, GDK_CONTROL_MASK, "page.zoom-out", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_0, GDK_CONTROL_MASK, "page.zoom-one", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_l, GDK_CONTROL_MASK, "buffer.select-line", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_d, GDK_CONTROL_MASK, "buffer.delete-line", NULL);
}

static void
editor_source_view_init (EditorSourceView *self)
{
  g_autoptr(EditorJoinedMenu) joined = NULL;
  g_autoptr(GMenu) gsv_section = NULL;
  g_autoptr(GMenu) spell_section = NULL;
  GtkEventController *controller;
  GtkStyleContext *style_context;
  GMenuModel *extra_menu;

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "spelling.add", FALSE);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "spelling.ignore", FALSE);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "spelling.correct", FALSE);

  self->css_provider = gtk_css_provider_new ();
  style_context = gtk_widget_get_style_context (GTK_WIDGET (self));
  gtk_style_context_add_provider (style_context,
                                  GTK_STYLE_PROVIDER (self->css_provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

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

  tweak_gutter_spacing (GTK_SOURCE_VIEW (self));

  joined = editor_joined_menu_new ();
  gsv_section = g_menu_new ();
  spell_section = g_menu_new ();

  extra_menu = gtk_text_view_get_extra_menu (GTK_TEXT_VIEW (self));
  g_menu_append_section (gsv_section, NULL, extra_menu);
  editor_joined_menu_append_menu (joined, G_MENU_MODEL (gsv_section));

  self->spelling_menu = editor_spell_menu_new ();
  g_menu_append_section (spell_section, NULL, G_MENU_MODEL (self->spelling_menu));
  editor_joined_menu_append_menu (joined, G_MENU_MODEL (spell_section));

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
}
