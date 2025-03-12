/* editor-preferences-dialog.c
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

#include <adwaita.h>
#include <gtksourceview/gtksource.h>

#include "editor-indent-model.h"
#include "editor-page.h"
#include "editor-preferences-dialog-private.h"
#include "editor-preferences-font.h"
#include "editor-preferences-radio.h"
#include "editor-preferences-spin.h"
#include "editor-preferences-switch.h"
#include "editor-recoloring-private.h"
#include "editor-session.h"
#include "editor-utils-private.h"
#include "editor-window.h"

struct _EditorPreferencesDialog
{
  AdwPreferencesDialog  parent_instance;

  EditorWindow         *window;

  GSettings            *settings;
  GtkCssProvider       *css_provider;

  GtkSwitch            *use_custom_font;
  AdwSwitchRow         *show_grid;
  AdwSwitchRow         *restore_session;
  GtkFlowBox           *scheme_group;
  GtkSourceBuffer      *buffer;
  GtkSourceView        *source_view;
  GtkAdjustment        *indent_width_adjustment;
  GtkAdjustment        *tab_width_adjustment;
  AdwComboRow          *character_row;

  guint                 changing_indent_width : 1;
  guint                 disposed : 1;
};

enum {
  PROP_0,
  PROP_WINDOW,
  N_PROPS
};

G_DEFINE_TYPE (EditorPreferencesDialog, editor_preferences_dialog, ADW_TYPE_PREFERENCES_DIALOG)

static GParamSpec *properties [N_PROPS];

/* TODO: How and what should be translated here? */
static const struct {
  const char *id;
  const char *example;
} lang_previews[] = {
  { "markdown",
    "# Markdown\n"
    " 1. Numbered Lists\n"
    " * Unnumbered and [Links](https://gnome.org)\n"
    " * `Preformatted Text`\n"
    " * _Emphasis_ or *Emphasis* **Combined**\n"
    "> Block quotes too!"
  },
  { "c",
    "#include <stdio.h>\n"
    "#define EXIT_SUCCESS 0\n"
    "static const struct { int i; double d; char c; } alias;\n"
    "int main (int argc, char *argv[]) {\n"
    "  printf (\"Hello, World! %d %f\\n\", 1, 2.0);\n"
    "  return EXIT_SUCCESS; /* comment */\n"
    "}"
  },
  { "python3",
    "from gi.repository import GObject, Gtk\n"
    "class MyObject(GObject.Object):\n"
    "    def __init__(self, *args, **kwargs):\n"
    "        super().__init__(*args, **kwargs)\n"
  },
  {
    "js",
    "/* JavaScript */\n"
    "const { GLib, Gtk } = imports.gi;\n"
    "var MyObject = class MyObject extends GLib.Object {\n"
    "    constructor(params = {}) {\n"
    "      super();\n"
    "      this._field = null;\n"
    "    }\n"
    "}"
  },
};

static const char *
get_preview (GtkSourceLanguage *lang)
{
  if (lang != NULL)
    {
      const char *id = gtk_source_language_get_id (lang);

      for (guint i = 0; i < G_N_ELEMENTS (lang_previews); i++)
        {
          if (g_strcmp0 (lang_previews[i].id, id) == 0)
            return lang_previews[i].example;
        }
    }

  return NULL;
}

static void
set_preview_language (EditorPreferencesDialog *self,
                      GtkSourceLanguage       *lang,
                      const char              *text)
{
  GtkTextIter iter;

  if (lang == NULL && text == NULL)
    {
      GtkSourceLanguageManager *manager = gtk_source_language_manager_get_default ();
      lang = gtk_source_language_manager_get_language (manager, lang_previews[0].id);
      text = lang_previews[0].example;
    }

  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (self->buffer), text, -1);
  gtk_source_buffer_set_language (self->buffer, lang);
  gtk_source_buffer_set_highlight_syntax (self->buffer, lang != NULL);

  gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (self->buffer), &iter, 3);
  gtk_text_buffer_select_range (GTK_TEXT_BUFFER (self->buffer), &iter, &iter);
}

static void
guess_preview_language (EditorPreferencesDialog *self)
{
  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));

  if (EDITOR_IS_WINDOW (self->window))
    {
      EditorDocument *document;
      EditorPage *page;

      if ((page = editor_window_get_visible_page (self->window)) &&
          (document = editor_page_get_document (page)))
        {
          GtkSourceLanguage *lang = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (document));
          const char *text = get_preview (lang);

          if (text)
            {
              set_preview_language (self, lang, text);
              return;
            }
        }
    }

  set_preview_language (self, NULL, NULL);
}

static void
update_style_scheme_selection (EditorPreferencesDialog *self)
{
  const char *id;

  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));

  id = editor_application_get_style_scheme (EDITOR_APPLICATION (g_application_get_default ()));

  for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self->scheme_group));
       child;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkFlowBoxChild *intermediate = GTK_FLOW_BOX_CHILD (child);
      GtkSourceStyleSchemePreview *preview = GTK_SOURCE_STYLE_SCHEME_PREVIEW (gtk_flow_box_child_get_child (intermediate));
      GtkSourceStyleScheme *scheme = gtk_source_style_scheme_preview_get_scheme (preview);
      const char *scheme_id = gtk_source_style_scheme_get_id (scheme);
      gboolean selected = g_strcmp0 (scheme_id, id) == 0;

      gtk_source_style_scheme_preview_set_selected (preview, selected);

      if (selected)
        gtk_source_buffer_set_style_scheme (self->buffer, scheme);
    }
}

typedef struct
{
  const char           *id;
  const char           *sort_key;
  GtkSourceStyleScheme *scheme;
  guint                 has_alt : 1;
  guint                 is_dark : 1;
} SchemeInfo;

static int
sort_schemes_cb (gconstpointer a,
                 gconstpointer b)
{
  const SchemeInfo *info_a = a;
  const SchemeInfo *info_b = b;

  /* Light schemes first */
  if (!info_a->is_dark && info_b->is_dark)
    return -1;
  else if (info_a->is_dark && !info_b->is_dark)
    return 1;

  /* Items with variants first */
  if (info_a->has_alt && !info_b->has_alt)
    return -1;
  else if (!info_a->has_alt && info_b->has_alt)
    return 1;

  return g_utf8_collate (info_a->sort_key, info_b->sort_key);
}

static void
update_style_schemes (EditorPreferencesDialog *self)
{
  GtkSourceStyleSchemeManager *sm;
  const char * const *scheme_ids;
  g_autoptr(GArray) schemes = NULL;
  const char *current_scheme;
  gboolean is_dark;
  guint j = 0;
  GtkWidget *child;

  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));

  schemes = g_array_new (FALSE, FALSE, sizeof (SchemeInfo));
  is_dark = adw_style_manager_get_dark (adw_style_manager_get_default ());
  current_scheme = editor_application_get_style_scheme (EDITOR_APPLICATION_DEFAULT);

  /* Populate schemes for preferences */
  sm = gtk_source_style_scheme_manager_get_default ();
  if ((scheme_ids = gtk_source_style_scheme_manager_get_scheme_ids (sm)))
    {
      for (guint i = 0; scheme_ids[i]; i++)
        {
          SchemeInfo info;

          /* Ignore our printing scheme */
          if (g_strcmp0 (scheme_ids[i], "printing") == 0)
            continue;

          info.scheme = gtk_source_style_scheme_manager_get_scheme (sm, scheme_ids[i]);
          info.id = gtk_source_style_scheme_get_id (info.scheme);
          info.sort_key = gtk_source_style_scheme_get_name (info.scheme);
          info.has_alt = FALSE;
          info.is_dark = FALSE;

          if (_editor_source_style_scheme_is_dark (info.scheme))
            {
              GtkSourceStyleScheme *alt = _editor_source_style_scheme_get_variant (info.scheme, "light");

              g_assert (GTK_SOURCE_IS_STYLE_SCHEME (alt));

              if (alt != info.scheme)
                {
                  info.sort_key = gtk_source_style_scheme_get_id (alt);
                  info.has_alt = TRUE;
                }

              info.is_dark = TRUE;
            }
          else
            {
              GtkSourceStyleScheme *alt = _editor_source_style_scheme_get_variant (info.scheme, "dark");

              g_assert (GTK_SOURCE_IS_STYLE_SCHEME (alt));

              if (alt != info.scheme)
                info.has_alt = TRUE;
            }

          g_array_append_val (schemes, info);
        }

      g_array_sort (schemes, sort_schemes_cb);
    }

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self->scheme_group))))
    gtk_flow_box_remove (self->scheme_group, child);

  for (guint i = 0; i < schemes->len; i++)
    {
      const SchemeInfo *info = &g_array_index (schemes, SchemeInfo, i);
      GtkWidget *preview;

      /* Ignore if not matching light/dark variant for app, unless it is
       * the current scheme and it has no alternate.
       */
      if (is_dark != _editor_source_style_scheme_is_dark (info->scheme) &&
          (g_strcmp0 (info->id, current_scheme) != 0 || info->has_alt))
        continue;

      preview = gtk_source_style_scheme_preview_new (info->scheme);
      gtk_actionable_set_action_name (GTK_ACTIONABLE (preview), "app.style-scheme");
      gtk_actionable_set_action_target (GTK_ACTIONABLE (preview), "s", info->id);
      gtk_flow_box_insert (self->scheme_group, preview, -1);

      j++;
    }

  update_style_scheme_selection (self);
}

static gboolean
bind_background_pattern (GValue *value,
                         GVariant *variant,
                         gpointer user_data)
{
  if (g_variant_get_boolean (variant))
    g_value_set_enum (value, GTK_SOURCE_BACKGROUND_PATTERN_TYPE_GRID);
  else
    g_value_set_enum (value, GTK_SOURCE_BACKGROUND_PATTERN_TYPE_NONE);
  return TRUE;
}

static void
update_custom_font_cb (EditorPreferencesDialog *self,
                       const char              *key,
                       GSettings               *settings)
{
  g_autofree char *custom_font = NULL;
  g_autoptr(GString) str = NULL;
  gboolean use_system_font;
  double line_height;
  char line_height_str[G_ASCII_DTOSTR_BUF_SIZE];

  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));
  g_assert (G_IS_SETTINGS (settings));

  line_height = g_settings_get_double (settings, "line-height");
  use_system_font = g_settings_get_boolean (settings, "use-system-font");
  custom_font = g_settings_get_string (settings, "custom-font");

  str = g_string_new ("textview {\n");

  if (!use_system_font)
    {
      PangoFontDescription *font_desc = pango_font_description_from_string (custom_font);

      if (font_desc != NULL)
        {
          g_autofree char *css = _editor_font_description_to_css (font_desc);

          if (css != NULL)
            g_string_append_printf (str, "  %s\n", css);

          pango_font_description_free (font_desc);
        }
    }

  g_ascii_dtostr (line_height_str, sizeof line_height_str, line_height);
  line_height_str[6] = 0;
  g_string_append_printf (str, "  line-height: %s;\n", line_height_str);

  g_string_append (str, "}");

  gtk_css_provider_load_from_string (self->css_provider, str->str);
}

static void
style_scheme_activated_cb (EditorPreferencesDialog *self,
                           GtkFlowBoxChild         *child,
                           GtkFlowBox              *flow_box)
{
  GtkWidget *preview;

  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));
  g_assert (GTK_IS_FLOW_BOX_CHILD (child));
  g_assert (GTK_IS_FLOW_BOX (flow_box));

  if ((preview = gtk_flow_box_child_get_child (child)))
    {
      g_assert (GTK_SOURCE_IS_STYLE_SCHEME_PREVIEW (preview));
      gtk_widget_activate (preview);
    }
}

static gboolean
can_install_scheme (GtkSourceStyleSchemeManager *manager,
                    const char * const          *scheme_ids,
                    GFile                       *file)
{
  g_autofree char *uri = NULL;
  const char *path;

  g_assert (GTK_SOURCE_IS_STYLE_SCHEME_MANAGER (manager));
  g_assert (G_IS_FILE (file));

  uri = g_file_get_uri (file);

  /* Don't allow resources, which would be weird anyway */
  if (g_str_has_prefix (uri, "resource://"))
    return FALSE;

  /* Make sure it's in the form of name.xml as we will require
   * that elsewhere anyway.
   */
  if (!g_str_has_suffix (uri, ".xml"))
    return FALSE;

  /* Not a native file, so likely not already installed */
  if (!g_file_is_native (file))
    return TRUE;

  path = g_file_peek_path (file);
  scheme_ids = gtk_source_style_scheme_manager_get_scheme_ids (manager);
  for (guint i = 0; scheme_ids[i] != NULL; i++)
    {
      GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme (manager, scheme_ids[i]);
      const char *filename = gtk_source_style_scheme_get_filename (scheme);

      /* If we have already loaded this scheme, then ignore it */
      if (g_strcmp0 (filename, path) == 0)
        return FALSE;
    }

  return TRUE;
}

static void
editor_preferences_dialog_install_schemes_cb (GObject      *object,
                                              GAsyncResult *result,
                                              gpointer      user_data)
{
  EditorApplication *app = (EditorApplication *)object;
  g_autoptr(EditorPreferencesDialog) self = user_data;
  GtkSourceStyleSchemeManager *manager;
  g_autoptr(GError) error = NULL;

  g_assert (EDITOR_IS_APPLICATION (app));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));

  if (!editor_application_install_schemes_finish (app, result, &error))
    g_critical ("Failed to install schemes: %s", error->message);

  manager = gtk_source_style_scheme_manager_get_default ();
  gtk_source_style_scheme_manager_force_rescan (manager);

  if (!self->disposed)
    update_style_schemes (self);
}

static gboolean
editor_preferences_dialog_drop_scheme_cb (EditorPreferencesDialog *self,
                                          const GValue            *value,
                                          double                   x,
                                          double                   y,
                                          GtkDropTarget           *drop_target)
{
  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));
  g_assert (GTK_IS_DROP_TARGET (drop_target));

  if (G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
    {
      GSList *list = g_value_get_boxed (value);
      g_autoptr(GPtrArray) to_install = NULL;
      GtkSourceStyleSchemeManager *manager;
      const char * const *scheme_ids;

      if (list == NULL)
        return FALSE;

      manager = gtk_source_style_scheme_manager_get_default ();
      scheme_ids = gtk_source_style_scheme_manager_get_scheme_ids (manager);
      to_install = g_ptr_array_new_with_free_func (g_object_unref);

      for (const GSList *iter = list; iter; iter = iter->next)
        {
          GFile *file = iter->data;

          if (can_install_scheme (manager, scheme_ids, file))
            g_ptr_array_add (to_install, g_object_ref (file));
        }

      if (to_install->len == 0)
        return FALSE;

      editor_application_install_schemes_async (EDITOR_APPLICATION_DEFAULT,
                                                (GFile **)(gpointer)to_install->pdata,
                                                to_install->len,
                                                NULL,
                                                editor_preferences_dialog_install_schemes_cb,
                                                g_object_ref (self));

      return TRUE;
    }

  return FALSE;
}

static gboolean
style_to_selected (GValue   *to_value,
                   GVariant *from_value,
                   gpointer  user_data)
{
  const char *str = g_variant_get_string (from_value, NULL);
  gboolean insert_spaces = g_strcmp0 (str, "space") == 0;
  g_value_set_uint (to_value, editor_indent_get_index_for (insert_spaces));
  return TRUE;
}

static GVariant *
selected_to_style (const GValue       *from_value,
                   const GVariantType *type,
                   gpointer            user_data)
{
  if (g_value_get_uint (from_value) == 0)
    return g_variant_new_string ("space");
  else
    return g_variant_new_string ("tab");
}

static void
on_indent_value_changed_cb (EditorPreferencesDialog *self,
                            GtkAdjustment           *adjustment)
{
  guint tab_width;
  int indent_width;

  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));
  g_assert (GTK_IS_ADJUSTMENT (adjustment));

  if (self->changing_indent_width)
    return;

  indent_width = gtk_adjustment_get_value (adjustment);
  tab_width = g_settings_get_uint (self->settings, "tab-width");

  if (indent_width == tab_width)
    indent_width = -1;

  self->changing_indent_width = TRUE;
  g_settings_set_int (self->settings, "indent-width", indent_width);
  self->changing_indent_width = FALSE;
}

static void
update_indent_width_cb (EditorPreferencesDialog *self,
                        const char              *key,
                        GSettings               *settings)
{
  guint tab_width;
  int indent_width;

  g_assert (EDITOR_IS_PREFERENCES_DIALOG (self));
  g_assert (G_IS_SETTINGS (settings));

  if (self->changing_indent_width)
    return;

  tab_width = g_settings_get_uint (settings, "tab-width");
  indent_width = g_settings_get_int (settings, "indent-width");

  if (indent_width <= 0)
    indent_width = tab_width;

  self->changing_indent_width = TRUE;
  gtk_adjustment_set_value (self->indent_width_adjustment, indent_width);
  self->changing_indent_width = FALSE;
}

static void
editor_preferences_dialog_constructed (GObject *object)
{
  EditorPreferencesDialog *self = (EditorPreferencesDialog *)object;
  g_autoptr(GSettings) settings = g_settings_new ("org.gnome.TextEditor");

  G_OBJECT_CLASS (editor_preferences_dialog_parent_class)->constructed (object);

  update_style_schemes (self);
  guess_preview_language (self);
  update_custom_font_cb (self, NULL, self->settings);

  /* Only show parameter if it is currently enabled */
  if (g_settings_get_boolean (settings, "show-grid"))
    gtk_widget_set_visible (GTK_WIDGET (self->show_grid), TRUE);
}

static void
editor_preferences_dialog_dispose (GObject *object)
{
  EditorPreferencesDialog *self = (EditorPreferencesDialog *)object;

  self->disposed = TRUE;

  g_clear_weak_pointer (&self->window);
  g_clear_object (&self->settings);
  g_clear_object (&self->css_provider);

  G_OBJECT_CLASS (editor_preferences_dialog_parent_class)->dispose (object);
}

static void
editor_preferences_dialog_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  EditorPreferencesDialog *self = EDITOR_PREFERENCES_DIALOG (object);

  switch (prop_id)
    {
    case PROP_WINDOW:
      g_value_set_object (value, self->window);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_preferences_dialog_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  EditorPreferencesDialog *self = EDITOR_PREFERENCES_DIALOG (object);

  switch (prop_id)
    {
    case PROP_WINDOW:
      g_set_weak_pointer (&self->window, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_preferences_dialog_class_init (EditorPreferencesDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = editor_preferences_dialog_constructed;
  object_class->dispose = editor_preferences_dialog_dispose;
  object_class->get_property = editor_preferences_dialog_get_property;
  object_class->set_property = editor_preferences_dialog_set_property;

  properties [PROP_WINDOW] =
    g_param_spec_object ("window",
                         "Window",
                         "The editor window",
                         EDITOR_TYPE_WINDOW,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-preferences-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, buffer);
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, character_row);
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, indent_width_adjustment);
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, restore_session);
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, scheme_group);
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, show_grid);
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, source_view);
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, tab_width_adjustment);
  gtk_widget_class_bind_template_child (widget_class, EditorPreferencesDialog, use_custom_font);

  gtk_widget_class_bind_template_callback (widget_class, style_scheme_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_indent_value_changed_cb);

  g_type_ensure (EDITOR_TYPE_INDENT_MODEL);
  g_type_ensure (EDITOR_TYPE_PREFERENCES_FONT);
  g_type_ensure (EDITOR_TYPE_PREFERENCES_SPIN);
  g_type_ensure (EDITOR_TYPE_PREFERENCES_SWITCH);
}

static void
editor_preferences_dialog_init (EditorPreferencesDialog *self)
{
  AdwStyleManager *style_manager;
  GtkDropTarget *drop_target;

  gtk_widget_init_template (GTK_WIDGET (self));

#if DEVELOPMENT_BUILD
  gtk_widget_add_css_class (GTK_WIDGET (self), "devel");
#endif

  drop_target = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
  g_signal_connect_object (drop_target,
                           "drop",
                           G_CALLBACK (editor_preferences_dialog_drop_scheme_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (drop_target));

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    {
      GtkStyleContext *style_context = gtk_widget_get_style_context (GTK_WIDGET (self->source_view));
      self->css_provider = gtk_css_provider_new ();
      gtk_style_context_add_provider (style_context,
                                      GTK_STYLE_PROVIDER (self->css_provider),
                                      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
  G_GNUC_END_IGNORE_DEPRECATIONS

  self->settings = g_settings_new ("org.gnome.TextEditor");
  g_settings_bind (self->settings, "use-system-font",
                   self->use_custom_font, "active",
                   G_SETTINGS_BIND_DEFAULT | G_SETTINGS_BIND_INVERT_BOOLEAN);
  g_settings_bind (self->settings, "highlight-current-line",
                   self->source_view, "highlight-current-line",
                   G_SETTINGS_BIND_GET);
  g_settings_bind (self->settings, "show-right-margin",
                   self->source_view, "show-right-margin",
                   G_SETTINGS_BIND_GET);
  g_settings_bind (self->settings, "show-line-numbers",
                   self->source_view, "show-line-numbers",
                   G_SETTINGS_BIND_GET);
  g_settings_bind_with_mapping (self->settings, "show-grid",
                                self->source_view, "background-pattern",
                                G_SETTINGS_BIND_GET,
                                bind_background_pattern, NULL, NULL, NULL);
  g_settings_bind (self->settings, "restore-session",
                   self->restore_session, "active",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "tab-width",
                   self->tab_width_adjustment, "value",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind_with_mapping (self->settings, "indent-style",
                                self->character_row, "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                style_to_selected, selected_to_style, NULL, NULL);
  g_signal_connect_object (self->settings,
                           "changed::custom-font",
                           G_CALLBACK (update_custom_font_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->settings,
                           "changed::use-system-font",
                           G_CALLBACK (update_custom_font_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->settings,
                           "changed::line-height",
                           G_CALLBACK (update_custom_font_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->settings,
                           "changed::indent-width",
                           G_CALLBACK (update_indent_width_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->settings,
                           "changed::tab-width",
                           G_CALLBACK (update_indent_width_cb),
                           self,
                           G_CONNECT_SWAPPED);
  update_indent_width_cb (self, NULL, self->settings);

  g_signal_connect_object (g_application_get_default (),
                           "notify::style-scheme",
                           G_CALLBACK (update_style_scheme_selection),
                           self,
                           G_CONNECT_SWAPPED);

  style_manager = adw_style_manager_get_default ();
  g_signal_connect_object (style_manager,
                           "notify::dark",
                           G_CALLBACK (update_style_schemes),
                           self,
                           G_CONNECT_SWAPPED);
}

AdwDialog *
editor_preferences_dialog_new (EditorWindow *window)
{
  g_return_val_if_fail (EDITOR_IS_WINDOW (window), NULL);

  return g_object_new (EDITOR_TYPE_PREFERENCES_DIALOG,
                       "window", window,
                       NULL);
}
