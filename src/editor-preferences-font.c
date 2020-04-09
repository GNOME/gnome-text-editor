/* editor-preferences-font.c
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

#define G_LOG_DOMAIN "editor-preferences-font"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-preferences-font.h"

struct _EditorPreferencesFont
{
  EditorPreferencesRow  row;

  GtkLabel             *label;
  GtkLabel             *font_label;
  GtkPopover           *popover;
  GtkFontChooserWidget *chooser;

  GSettings            *settings;
  gchar                *schema_id;
  gchar                *schema_key;
};

enum {
  PROP_0,
  PROP_LABEL,
  PROP_SCHEMA_ID,
  PROP_SCHEMA_KEY,
  N_PROPS
};

G_DEFINE_TYPE (EditorPreferencesFont, editor_preferences_font, EDITOR_TYPE_PREFERENCES_ROW)

static GParamSpec *properties [N_PROPS];

static void
editor_preferences_font_dispose (GObject *object)
{
  EditorPreferencesFont *self = (EditorPreferencesFont *)object;

  if (self->popover != NULL)
    {
      gtk_widget_unparent (GTK_WIDGET (self->popover));
      self->popover = NULL;
    }

  G_OBJECT_CLASS (editor_preferences_font_parent_class)->dispose (object);
}

static void
editor_preferences_font_constructed (GObject *object)
{
  EditorPreferencesFont *self = (EditorPreferencesFont *)object;

  G_OBJECT_CLASS (editor_preferences_font_parent_class)->constructed (object);

  if (self->schema_id == NULL || self->schema_key == NULL)
    {
      g_warning ("Cannot setup preferences switch, missing schema properties");
      return;
    }

  self->settings = g_settings_new (self->schema_id);

  g_settings_bind (self->settings, "custom-font",
                   self->font_label, "label",
                   G_SETTINGS_BIND_GET);

  g_settings_bind (self->settings, "custom-font",
                   self->chooser, "font",
                   G_SETTINGS_BIND_DEFAULT);
}

static void
editor_preferences_font_activated (EditorPreferencesRow *row)
{
  EditorPreferencesFont *self = (EditorPreferencesFont *)row;

  g_assert (EDITOR_IS_PREFERENCES_ROW (self));

  gtk_popover_popup (GTK_POPOVER (self->popover));
}

static void
editor_preferences_font_remove (GtkContainer *container,
                                GtkWidget    *widget)
{
  EditorPreferencesFont *self = (EditorPreferencesFont *)container;

  g_assert (EDITOR_IS_PREFERENCES_FONT (self));
  g_assert (GTK_IS_WIDGET (widget));

  if (widget == GTK_WIDGET (self->popover))
    {
      self->popover = NULL;
      gtk_widget_unparent (widget);
      return;
    }

  GTK_CONTAINER_CLASS (editor_preferences_font_parent_class)->remove (container, widget);
}

static void
editor_preferences_font_size_allocate (GtkWidget *widget,
                                       int        width,
                                       int        height,
                                       int        baseline)
{
  EditorPreferencesFont *self = (EditorPreferencesFont *)widget;

  g_assert (EDITOR_IS_PREFERENCES_FONT (self));

  GTK_WIDGET_CLASS (editor_preferences_font_parent_class)->size_allocate (widget, width, height, baseline);

  gtk_native_check_resize (GTK_NATIVE (self->popover));
}

static void
editor_preferences_font_snapshot (GtkWidget   *widget,
                                  GtkSnapshot *snapshot)
{
  EditorPreferencesFont *self = (EditorPreferencesFont *)widget;

  g_assert (EDITOR_IS_PREFERENCES_FONT (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  GTK_WIDGET_CLASS (editor_preferences_font_parent_class)->snapshot (widget, snapshot);

  gtk_widget_snapshot_child (widget, GTK_WIDGET (self->popover), snapshot);
}

static void
editor_preferences_font_finalize (GObject *object)
{
  EditorPreferencesFont *self = (EditorPreferencesFont *)object;

  g_clear_object (&self->settings);
  g_clear_pointer (&self->schema_id, g_free);
  g_clear_pointer (&self->schema_key, g_free);

  G_OBJECT_CLASS (editor_preferences_font_parent_class)->finalize (object);
}

static void
editor_preferences_font_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  EditorPreferencesFont *self = EDITOR_PREFERENCES_FONT (object);

  switch (prop_id)
    {
    case PROP_LABEL:
      g_value_set_string (value, gtk_label_get_label (self->label));
      break;

    case PROP_SCHEMA_ID:
      g_value_set_string (value, self->schema_id);
      break;

    case PROP_SCHEMA_KEY:
      g_value_set_string (value, self->schema_key);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_preferences_font_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  EditorPreferencesFont *self = EDITOR_PREFERENCES_FONT (object);

  switch (prop_id)
    {
    case PROP_LABEL:
      gtk_label_set_label (self->label, g_value_get_string (value));
      break;

    case PROP_SCHEMA_ID:
      self->schema_id = g_value_dup_string (value);
      break;

    case PROP_SCHEMA_KEY:
      self->schema_key = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_preferences_font_class_init (EditorPreferencesFontClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);
  EditorPreferencesRowClass *row_class = EDITOR_PREFERENCES_ROW_CLASS (klass);

  object_class->constructed = editor_preferences_font_constructed;
  object_class->dispose = editor_preferences_font_dispose;
  object_class->finalize = editor_preferences_font_finalize;
  object_class->get_property = editor_preferences_font_get_property;
  object_class->set_property = editor_preferences_font_set_property;

  widget_class->size_allocate = editor_preferences_font_size_allocate;
  widget_class->snapshot = editor_preferences_font_snapshot;

  container_class->remove = editor_preferences_font_remove;

  row_class->activated = editor_preferences_font_activated;

  properties [PROP_LABEL] =
    g_param_spec_string ("label",
                         "Label",
                         "The label for the row",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SCHEMA_ID] =
    g_param_spec_string ("schema-id",
                         "Schema Id",
                         "The identifier of the GSettings schema",
                         "org.gnome.TextEditor",
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SCHEMA_KEY] =
    g_param_spec_string ("schema-key",
                         "Schema Key",
                         "The key within the GSettings schema",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  
  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_preferences_font_init (EditorPreferencesFont *self)
{
  GtkBox *box;
  GtkImage *image;

  box = g_object_new (GTK_TYPE_BOX,
                      "can-focus", FALSE,
                      "valign", GTK_ALIGN_CENTER,
                      "margin-start", 20,
                      "margin-end", 20,
                      "spacing", 10,
                      NULL);
  gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (box));

  self->label = g_object_new (GTK_TYPE_LABEL,
                              "can-focus", FALSE,
                              "selectable", FALSE,
                              "halign", GTK_ALIGN_START,
                              "hexpand", FALSE,
                              NULL);
  gtk_container_add (GTK_CONTAINER (box), GTK_WIDGET (self->label));

  self->font_label = g_object_new (GTK_TYPE_LABEL,
                                   "can-focus", FALSE,
                                   "ellipsize", PANGO_ELLIPSIZE_MIDDLE,
                                   "selectable", FALSE,
                                   "halign", GTK_ALIGN_END,
                                   "hexpand", TRUE,
                                   NULL);
  gtk_container_add (GTK_CONTAINER (box), GTK_WIDGET (self->font_label));

  image = g_object_new (GTK_TYPE_IMAGE,
                        "icon-name", "go-next-symbolic",
                        "hexpand", FALSE,
                        "margin-start", 12,
                        NULL);
  gtk_container_add (GTK_CONTAINER (box), GTK_WIDGET (image));

  self->popover = g_object_new (GTK_TYPE_POPOVER,
                                "position", GTK_POS_RIGHT,
                                NULL);
  gtk_widget_set_parent (GTK_WIDGET (self->popover), GTK_WIDGET (self));

  self->chooser = g_object_new (GTK_TYPE_FONT_CHOOSER_WIDGET, NULL);
  gtk_container_add (GTK_CONTAINER (self->popover), GTK_WIDGET (self->chooser));
}
