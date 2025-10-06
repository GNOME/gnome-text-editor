/* editor-sidebar-item.c
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

#define G_LOG_DOMAIN "editor-sidebar-item"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-application.h"
#include "editor-document-private.h"
#include "editor-page-private.h"
#include "editor-path-private.h"
#include "editor-session-private.h"
#include "editor-sidebar-item-private.h"
#include "editor-window.h"

struct _EditorSidebarItem
{
  GObject     parent_instance;

  GFile      *file;
  EditorPage *page;
  gchar      *draft_id;
  gchar      *title;
  gchar      *subtitle;
  gint64      age;

  GPtrArray  *keywords;

  guint       is_modified_set : 1;
  guint       is_modified : 1;
};

enum {
  PROP_0,
  PROP_AGE,
  PROP_DRAFT_ID,
  PROP_EMPTY,
  PROP_FILE,
  PROP_IS_MODIFIED,
  PROP_PAGE,
  PROP_SUBTITLE,
  PROP_TITLE,
  N_PROPS
};

G_DEFINE_TYPE (EditorSidebarItem, editor_sidebar_item, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static gboolean
file_is_from_document_portal (GFile *file)
{
#ifdef G_OS_UNIX
  static char *docportal;

  if G_UNLIKELY (docportal == NULL)
    docportal = g_strdup_printf ("%s/doc/", g_get_user_runtime_dir ());

  if (g_file_is_native (file))
    {
      const char *path = g_file_peek_path (file);

      if (g_str_has_prefix (path, docportal))
        return TRUE;
    }
#endif

  return FALSE;
}

static void
editor_sidebar_item_update_subtitle (EditorSidebarItem *self)
{
  g_autoptr(GFile) dir = NULL;

  g_assert (EDITOR_IS_SIDEBAR_ITEM (self));

  g_free (self->subtitle);

  if (self->file == NULL)
    {
      self->subtitle = g_strdup (_("Draft"));
      return;
    }

  /* Can happen, but implausible since someone tried to open "/" */
  if (!(dir = g_file_get_parent (self->file)))
    self->subtitle = g_strdup ("");
  else if (file_is_from_document_portal (dir))
    self->subtitle = _editor_get_portal_host_path (dir);
  else if (g_file_is_native (dir))
    self->subtitle = _editor_path_collapse (g_file_peek_path (dir));
  else
    self->subtitle = g_file_get_uri (dir);
}

static void
editor_sidebar_item_do_notify (EditorSidebarItem *self)
{
  g_assert (EDITOR_IS_SIDEBAR_ITEM (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SUBTITLE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_IS_MODIFIED]);
}

static void
editor_sidebar_item_query_info_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GFileInfo) file_info = NULL;
  g_autoptr(EditorSidebarItem) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (EDITOR_IS_SIDEBAR_ITEM (self));

  if ((file_info = g_file_query_info_finish (file, result, &error)))
    self->age = g_file_info_get_attribute_uint64 (file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
  else
    self->age = 0;

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_AGE]);
}

void
_editor_sidebar_item_set_age (EditorSidebarItem *self,
                              gint64             age)
{
  g_assert (EDITOR_IS_SIDEBAR_ITEM (self));

  if (age != self->age)
    {
      self->age = age;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_AGE]);
    }
}

static void
editor_sidebar_item_set_file (EditorSidebarItem *self,
                              GFile             *file)
{
  g_return_if_fail (EDITOR_IS_SIDEBAR_ITEM (self));
  g_return_if_fail (!file || G_IS_FILE (file));

  if (g_set_object (&self->file, file))
    {
      if (file != NULL && g_file_is_native (file))
        {
          GDateTime *age = g_object_get_data (G_OBJECT (file), "AGE");

          if (age != NULL)
            self->age = g_date_time_to_unix (age);
          else
            g_file_query_info_async (file,
                                     G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                     G_FILE_QUERY_INFO_NONE,
                                     G_PRIORITY_LOW + 100,
                                     NULL,
                                     editor_sidebar_item_query_info_cb,
                                     g_object_ref (self));
        }

      editor_sidebar_item_do_notify (self);
      editor_sidebar_item_update_subtitle (self);
    }
}

static void
editor_sidebar_item_notify_is_modified_cb (EditorSidebarItem *self,
                                           GParamSpec        *pspec,
                                           EditorPage        *page)
{
  g_assert (EDITOR_IS_SIDEBAR_ITEM (self));
  g_assert (EDITOR_IS_PAGE (page));

  self->is_modified_set = TRUE;
  self->is_modified = editor_page_get_is_modified (page);

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_IS_MODIFIED]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_EMPTY]);
}

static void
editor_sidebar_item_notify_title_cb (EditorSidebarItem *self,
                                     GParamSpec        *pspec,
                                     EditorPage        *page)
{
  g_assert (EDITOR_IS_SIDEBAR_ITEM (self));
  g_assert (EDITOR_IS_PAGE (page));

  g_clear_pointer (&self->keywords, g_ptr_array_unref);

  g_clear_pointer (&self->title, g_free);
  self->title = editor_page_dup_title (page);

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
}

static void
editor_sidebar_item_notify_subtitle_cb (EditorSidebarItem *self,
                                        GParamSpec        *pspec,
                                        EditorPage        *page)
{
  g_assert (EDITOR_IS_SIDEBAR_ITEM (self));
  g_assert (EDITOR_IS_PAGE (page));

  g_clear_pointer (&self->keywords, g_ptr_array_unref);

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SUBTITLE]);
}

void
_editor_sidebar_item_set_page (EditorSidebarItem *self,
                               EditorPage        *page)
{
  g_return_if_fail (EDITOR_IS_SIDEBAR_ITEM (self));
  g_return_if_fail (!page || EDITOR_IS_PAGE (page));

  if (g_set_object (&self->page, page))
    {
      if (page != NULL)
        {
          EditorDocument *document = editor_page_get_document (page);
          const gchar *draft_id = _editor_document_get_draft_id (document);

          _editor_sidebar_item_set_draft_id (self, draft_id);

          self->is_modified = editor_page_get_is_modified (page);
          self->is_modified_set = TRUE;

          g_signal_connect_object (page,
                                   "notify::is-modified",
                                   G_CALLBACK (editor_sidebar_item_notify_is_modified_cb),
                                   self,
                                   G_CONNECT_SWAPPED);
          g_signal_connect_object (page,
                                   "notify::title",
                                   G_CALLBACK (editor_sidebar_item_notify_title_cb),
                                   self,
                                   G_CONNECT_SWAPPED);
          g_signal_connect_object (page,
                                   "notify::subtitle",
                                   G_CALLBACK (editor_sidebar_item_notify_subtitle_cb),
                                   self,
                                   G_CONNECT_SWAPPED);
        }

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_PAGE]);

      editor_sidebar_item_do_notify (self);
    }
}

static void
editor_sidebar_item_finalize (GObject *object)
{
  EditorSidebarItem *self = (EditorSidebarItem *)object;

  g_clear_object (&self->file);
  g_clear_object (&self->page);
  g_clear_pointer (&self->keywords, g_ptr_array_unref);
  g_clear_pointer (&self->draft_id, g_free);
  g_clear_pointer (&self->subtitle, g_free);

  G_OBJECT_CLASS (editor_sidebar_item_parent_class)->finalize (object);
}

static void
editor_sidebar_item_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EditorSidebarItem *self = EDITOR_SIDEBAR_ITEM (object);

  switch (prop_id)
    {
    case PROP_AGE:
      g_value_take_boxed (value, _editor_sidebar_item_get_age (self));
      break;

    case PROP_DRAFT_ID:
      g_value_set_string (value, self->draft_id);
      break;

    case PROP_EMPTY:
      g_value_set_boolean (value, _editor_sidebar_item_get_empty (self));
      break;

    case PROP_FILE:
      g_value_set_object (value, _editor_sidebar_item_get_file (self));
      break;

    case PROP_PAGE:
      g_value_set_object (value, _editor_sidebar_item_get_page (self));
      break;

    case PROP_IS_MODIFIED:
      g_value_set_boolean (value, _editor_sidebar_item_get_is_modified (self));
      break;

    case PROP_TITLE:
      if (self->title != NULL)
        g_value_set_string (value, self->title);
      else
        g_value_take_string (value, _editor_sidebar_item_dup_title (self));
      break;

    case PROP_SUBTITLE:
      g_value_take_string (value, _editor_sidebar_item_dup_subtitle (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_sidebar_item_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EditorSidebarItem *self = EDITOR_SIDEBAR_ITEM (object);

  switch (prop_id)
    {
    case PROP_DRAFT_ID:
      _editor_sidebar_item_set_draft_id (self, g_value_get_string (value));
      break;

    case PROP_FILE:
      editor_sidebar_item_set_file (self, g_value_get_object (value));
      break;

    case PROP_PAGE:
      _editor_sidebar_item_set_page (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_sidebar_item_class_init (EditorSidebarItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = editor_sidebar_item_finalize;
  object_class->get_property = editor_sidebar_item_get_property;
  object_class->set_property = editor_sidebar_item_set_property;

  properties [PROP_AGE] =
    g_param_spec_boxed ("age",
                        "Age",
                        "Age",
                        G_TYPE_DATE_TIME,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DRAFT_ID] =
    g_param_spec_string ("draft-id",
                         "Draft ID",
                         "The identifier for a draft",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_EMPTY] =
    g_param_spec_boolean ("empty",
                          "Empty",
                          "If the item contains an empty page",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILE] =
    g_param_spec_object ("file",
                         "File",
                         "The file represented, if any",
                         G_TYPE_FILE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_PAGE] =
    g_param_spec_object ("page",
                         "Page",
                         "The page represented, if any",
                         EDITOR_TYPE_PAGE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_IS_MODIFIED] =
    g_param_spec_boolean ("is-modified",
                          "Is Modified",
                          "If the page or draft is modified",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title for the row",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SUBTITLE] =
    g_param_spec_string ("subtitle",
                         "Subtitle",
                         "The subtitle for the row",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_sidebar_item_init (EditorSidebarItem *self)
{
  self->subtitle = g_strdup ("");
}

EditorSidebarItem *
_editor_sidebar_item_new (GFile      *file,
                          EditorPage *page)
{
  g_return_val_if_fail (!file || G_IS_FILE (file), NULL);
  g_return_val_if_fail (!page || EDITOR_IS_PAGE (page), NULL);

  return g_object_new (EDITOR_TYPE_SIDEBAR_ITEM,
                       "file", file,
                       "page", page,
                       NULL);
}

GFile *
_editor_sidebar_item_get_file (EditorSidebarItem *self)
{
  g_return_val_if_fail (EDITOR_IS_SIDEBAR_ITEM (self), NULL);

  return self->file;
}

EditorPage *
_editor_sidebar_item_get_page (EditorSidebarItem *self)
{
  g_return_val_if_fail (EDITOR_IS_SIDEBAR_ITEM (self), NULL);

  return self->page;
}

gboolean
_editor_sidebar_item_get_is_modified (EditorSidebarItem *self)
{
  g_return_val_if_fail (EDITOR_IS_SIDEBAR_ITEM (self), FALSE);

  if (self->is_modified_set && self->is_modified)
    return TRUE;

  if (self->page == NULL && self->file == NULL)
    return TRUE;

  return self->page != NULL &&
         editor_page_get_is_modified (self->page);
}

gchar *
_editor_sidebar_item_dup_title (EditorSidebarItem *self)
{
  char *title = NULL;

  g_return_val_if_fail (EDITOR_IS_SIDEBAR_ITEM (self), NULL);

  if (self->title != NULL)
    return g_strdup (self->title);

  if (self->page != NULL)
    return editor_page_dup_title (self->page);

  if (self->file == NULL)
    return g_strdup (_("New Document"));

  g_return_val_if_fail (G_IS_FILE (self->file), NULL);

  title = g_file_get_basename (self->file);

  if (!g_utf8_validate (title, -1, NULL))
    {
      char *tmp = g_utf8_make_valid (title, -1);;

      g_free (title);
      title = tmp;
    }

  return title;
}

gchar *
_editor_sidebar_item_dup_subtitle (EditorSidebarItem *self)
{
  g_return_val_if_fail (EDITOR_IS_SIDEBAR_ITEM (self), NULL);

  return g_strdup (self->subtitle);
}

void
_editor_sidebar_item_open (EditorSidebarItem *self,
                           EditorSession     *session,
                           EditorWindow      *window)
{
  g_return_if_fail (EDITOR_IS_SIDEBAR_ITEM (self));
  g_return_if_fail (EDITOR_IS_SESSION (session));
  g_return_if_fail (EDITOR_IS_WINDOW (window));
  g_return_if_fail (self->page || self->file || self->draft_id);

  if (self->page != NULL)
    _editor_page_raise (self->page);
  else if (self->file != NULL)
    /* TODO: This should stash the encoding and reuse it */
    editor_session_open (session, window, self->file, NULL);
  else if (self->draft_id != NULL)
    _editor_session_open_draft (session, window, self->draft_id);
  else
    g_warn_if_reached ();
}

gboolean
_editor_sidebar_item_get_empty (EditorSidebarItem *self)
{
  g_return_val_if_fail (EDITOR_IS_SIDEBAR_ITEM (self), FALSE);

  return self->page != NULL &&
         editor_page_is_draft (self->page) &&
         !editor_page_get_is_modified (self->page);
}

gboolean
_editor_sidebar_item_matches (EditorSidebarItem *self,
                              const char        *search,
                              guint             *out_score)
{
  gboolean matches = FALSE;
  guint best_score = G_MAXINT;

  if (search == NULL)
    return TRUE;

  if G_UNLIKELY (self->keywords == NULL)
    {
      g_autofree gchar *title = _editor_sidebar_item_dup_title (self);

      self->keywords = g_ptr_array_new_with_free_func (g_free);

      g_ptr_array_add (self->keywords, g_utf8_casefold (title, -1));
      g_ptr_array_add (self->keywords, g_utf8_casefold (self->subtitle, -1));
    }

  for (guint i = 0; i < self->keywords->len; i++)
    {
      const char *keyword = g_ptr_array_index (self->keywords, i);
      guint score = G_MAXINT;

      if (g_str_has_prefix (keyword, search))
        {
          matches = TRUE;
          best_score = 0;
          break;
        }
      else if (strstr (keyword, search) != NULL)
        {
          matches = TRUE;
          best_score = 1;
          break;
        }
      else if (gtk_source_completion_fuzzy_match (keyword, search, &score))
        {
          score += 2;
          best_score = MIN (score, best_score);
          matches = TRUE;
        }
    }

  if (out_score)
    *out_score = best_score;

  return matches;
}

void
_editor_sidebar_item_set_is_modified (EditorSidebarItem *self,
                                      gboolean           is_modified_set,
                                      gboolean           is_modified)
{
  g_return_if_fail (EDITOR_IS_SIDEBAR_ITEM (self));

  self->is_modified_set = !!is_modified_set;
  self->is_modified = !!is_modified;
}

void
_editor_sidebar_item_set_title (EditorSidebarItem *self,
                                const gchar       *title)
{
  g_return_if_fail (EDITOR_IS_SIDEBAR_ITEM (self));

  if (g_strcmp0 (title, self->title) != 0)
    {
      g_free (self->title);
      self->title = g_strdup (title);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
    }
}

const gchar *
_editor_sidebar_item_get_draft_id (EditorSidebarItem *self)
{
  g_return_val_if_fail (EDITOR_IS_SIDEBAR_ITEM (self), NULL);

  return self->draft_id;
}

void
_editor_sidebar_item_set_draft_id (EditorSidebarItem *self,
                                   const gchar       *draft_id)
{
  g_return_if_fail (EDITOR_IS_SIDEBAR_ITEM (self));

  if (g_strcmp0 (draft_id, self->draft_id) != 0)
    {
      g_free (self->draft_id);
      self->draft_id = g_strdup (draft_id);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_DRAFT_ID]);
    }
}

GDateTime *
_editor_sidebar_item_get_age (EditorSidebarItem *self)
{
  g_return_val_if_fail (EDITOR_IS_SIDEBAR_ITEM (self), NULL);

  if (self->age)
    return g_date_time_new_from_unix_local (self->age);

  return NULL;
}

int
_editor_sidebar_item_compare (EditorSidebarItem *a,
                              EditorSidebarItem *b)
{
  g_return_val_if_fail (EDITOR_IS_SIDEBAR_ITEM (a), 0);
  g_return_val_if_fail (EDITOR_IS_SIDEBAR_ITEM (b), 0);

  /* More recent sorts first (larger numeric value) */

  if (a->age < b->age)
    return 1;
  else if (a->age > b->age)
    return -1;
  else
    return 0;
}
