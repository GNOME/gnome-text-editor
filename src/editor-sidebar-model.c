/* editor-sidebar-model.c
 *
 * Copyright 2020-2023 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "editor-sidebar-model"

#include "config.h"

#include "editor-application.h"
#include "editor-document-private.h"
#include "editor-page-private.h"
#include "editor-session-private.h"
#include "editor-sidebar-item-private.h"
#include "editor-sidebar-model-private.h"
#include "editor-window.h"

struct _EditorSidebarModel
{
  GObject        parent_instance;
  GSequence     *seq;
  EditorSession *session;
  guint          update_timer;
  guint          length; /* cached for O(1) lookup */
};

enum {
  PROP_0,
  PROP_N_ITEMS,
  PROP_SESSION,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
items_changed (EditorSidebarModel *self,
               guint               position,
               guint               removed,
               guint               added)
{
  self->length -= removed;
  self->length += added;

  g_list_model_items_changed (G_LIST_MODEL (self), position, removed, added);

  if (removed != added)
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_N_ITEMS]);
}

static gboolean
update_timeout_cb (gpointer data)
{
  EditorSidebarModel *self = data;

  g_assert (EDITOR_IS_SIDEBAR_MODEL (self));

  for (GSequenceIter *iter = g_sequence_get_begin_iter (self->seq);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter))
    {
      EditorSidebarItem *item = g_sequence_get (iter);
      g_object_notify (G_OBJECT (item), "age");
    }

  return G_SOURCE_CONTINUE;
}

static guint
editor_sidebar_model_get_n_items (GListModel *model)
{
  EditorSidebarModel *self = EDITOR_SIDEBAR_MODEL (model);

  return self->length;
}

static GType
editor_sidebar_model_get_item_type (GListModel *model)
{
  return EDITOR_TYPE_SIDEBAR_ITEM;
}

static gpointer
editor_sidebar_model_get_item (GListModel *model,
                               guint       position)
{
  EditorSidebarModel *self = EDITOR_SIDEBAR_MODEL (model);
  GSequenceIter *iter;

  if (position >= self->length)
    return NULL;

  iter = g_sequence_get_iter_at_pos (self->seq, position);
  if (!g_sequence_iter_is_end (iter))
    return g_object_ref (g_sequence_get (iter));

  return NULL;
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_n_items = editor_sidebar_model_get_n_items;
  iface->get_item = editor_sidebar_model_get_item;
  iface->get_item_type = editor_sidebar_model_get_item_type;
}

G_DEFINE_TYPE_WITH_CODE (EditorSidebarModel, editor_sidebar_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static GSequenceIter *
find_by_draft_id (EditorSidebarModel *self,
                  const gchar        *draft_id)
{
  GSequenceIter *iter;

  g_assert (EDITOR_IS_SIDEBAR_MODEL (self));
  g_assert (draft_id != NULL);

  for (iter = g_sequence_get_begin_iter (self->seq);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter))
    {
      EditorSidebarItem *item = g_sequence_get (iter);
      const gchar *item_draft_id = _editor_sidebar_item_get_draft_id (item);

      if (item_draft_id != NULL && g_strcmp0 (item_draft_id, draft_id) == 0)
        return iter;
    }

  return NULL;
}

static GSequenceIter *
find_by_document (EditorSidebarModel *self,
                  EditorDocument     *document)
{
  GSequenceIter *iter;
  const gchar *draft_id;
  GFile *file;

  g_assert (EDITOR_IS_SIDEBAR_MODEL (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  draft_id = _editor_document_get_draft_id (document);
  file = editor_document_get_file (document);

  for (iter = g_sequence_get_begin_iter (self->seq);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter))
    {
      EditorSidebarItem *item = g_sequence_get (iter);
      GFile *item_file = _editor_sidebar_item_get_file (item);
      EditorPage *page = _editor_sidebar_item_get_page (item);
      const gchar *item_draft_id = _editor_sidebar_item_get_draft_id (item);

      if (file != NULL && item_file != NULL && g_file_equal (file, item_file))
        return iter;

      /* Maybe the draft-id match (for documents without a file yet) */
      if (item_draft_id != NULL && g_strcmp0 (item_draft_id, draft_id) == 0)
        return iter;

      if (page == NULL)
        continue;

      if (document == editor_page_get_document (page))
        return iter;
    }

  return NULL;
}

static GSequenceIter *
find_by_file (EditorSidebarModel *self,
              GFile              *file)
{
  GSequenceIter *iter;

  g_assert (EDITOR_IS_SIDEBAR_MODEL (self));
  g_assert (G_IS_FILE (file));

  for (iter = g_sequence_get_begin_iter (self->seq);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter))
    {
      EditorSidebarItem *item = g_sequence_get (iter);
      GFile *this_file = _editor_sidebar_item_get_file (item);

      if (this_file != NULL && g_file_equal (file, this_file))
        return iter;
    }

  return NULL;
}


static void
editor_sidebar_model_page_added_cb (EditorSidebarModel *self,
                                    EditorWindow       *window,
                                    EditorPage         *page,
                                    EditorSession      *session)
{
  EditorDocument *document;
  GSequenceIter *iter;

  g_assert (EDITOR_IS_SIDEBAR_MODEL (self));
  g_assert (EDITOR_IS_WINDOW (window));
  g_assert (EDITOR_IS_PAGE (page));
  g_assert (EDITOR_IS_SESSION (session));

  document = editor_page_get_document (page);

  /*
   * If we find the page that was opened already in our sidebar model,
   * then we want to remove that item. The sidebar is for recent files and
   * we don't want it to be cluttered with already open files which can be
   * opened from the tabs.
   */

  if ((iter = find_by_document (self, document)))
    {
      guint position = g_sequence_iter_get_position (iter);

      g_sequence_remove (iter);
      items_changed (self, position, 1, 0);
    }
}

static void
editor_sidebar_model_page_removed_cb (EditorSidebarModel *self,
                                      EditorWindow       *window,
                                      EditorPage         *page,
                                      EditorSession      *session)
{
  g_autoptr(EditorSidebarItem) item = NULL;
  g_autofree gchar *title = NULL;
  EditorDocument *document;
  GSequenceIter *iter;
  const gchar *draft_id;
  gboolean is_modified;
  GFile *file;

  g_assert (EDITOR_IS_SIDEBAR_MODEL (self));
  g_assert (EDITOR_IS_WINDOW (window));
  g_assert (EDITOR_IS_PAGE (page));
  g_assert (EDITOR_IS_SESSION (session));

  if (editor_page_get_can_discard (page))
    return;

  document = editor_page_get_document (page);
  file = editor_document_get_file (document);
  title = editor_document_dup_title (document);
  is_modified = gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (document));
  draft_id = _editor_document_get_draft_id (document);

  item = _editor_sidebar_item_new (file, NULL);
  _editor_sidebar_item_set_title (item, title);
  _editor_sidebar_item_set_is_modified (item, TRUE, is_modified);
  _editor_sidebar_item_set_draft_id (item, draft_id);
  _editor_sidebar_item_set_age (item, g_get_real_time ());

  iter = g_sequence_insert_sorted (self->seq,
                                   g_steal_pointer (&item),
                                   (GCompareDataFunc)_editor_sidebar_item_compare,
                                   NULL);

  items_changed (self,
                 g_sequence_iter_get_position (iter),
                 0, 1);
}

static void
on_notify_age_cb (EditorSidebarModel *self,
                  GParamSpec         *pspec,
                  EditorSidebarItem  *item)
{
  g_autoptr(GDateTime) dt = NULL;
  GSequenceIter *iter;
  GFile *file;
  guint old_position;
  guint new_position;

  g_assert (EDITOR_IS_SIDEBAR_MODEL (self));
  g_assert (EDITOR_IS_SIDEBAR_ITEM (item));

  g_signal_handlers_disconnect_by_func (item, G_CALLBACK (on_notify_age_cb), self);

  /* Ignore if we didn't discover a mtime */
  dt = _editor_sidebar_item_get_age (item);
  if (dt == NULL)
    return;

  file = _editor_sidebar_item_get_file (item);

  /* Ignore if we already removed this item */
  iter = find_by_file (self, file);
  if (iter == NULL)
    return;

  /* Remove the iter so we can place it somewhere new */
  old_position = g_sequence_iter_get_position (iter);
  g_object_ref (item);
  g_sequence_remove (iter);
  items_changed (self, old_position, 1, 0);
  iter = g_sequence_insert_sorted (self->seq,
                                   item,
                                   (GCompareDataFunc)_editor_sidebar_item_compare,
                                   NULL);
  new_position = g_sequence_iter_get_position (iter);
  items_changed (self, new_position, 0, 1);
}

static void
editor_sidebar_model_load_recent_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  EditorSession *session = (EditorSession *)object;
  g_autoptr(EditorSidebarModel) self = user_data;
  g_autoptr(GPtrArray) files = NULL;
  g_autoptr(GError) error = NULL;
  guint old_n_items;
  guint new_n_items;

  g_assert (EDITOR_IS_SESSION (session));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (EDITOR_IS_SIDEBAR_MODEL (self));

  if (!(files = editor_session_load_recent_finish (session, result, &error)))
    {
      if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("%s", error->message);
      return;
    }

  g_ptr_array_set_free_func (files, g_object_unref);

  old_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));

  for (guint i = 0; i < files->len; i++)
    {
      GFile *file = g_ptr_array_index (files, i);

      /* Skip this file if it is already open */
      if (editor_session_find_page_by_file (EDITOR_SESSION_DEFAULT, file))
        continue;

      if (!find_by_file (self, file))
        {
          g_autoptr(GDateTime) age = NULL;
          EditorSidebarItem *item;

          item = _editor_sidebar_item_new (file, NULL);
          age = _editor_sidebar_item_get_age (item);

          /* We need to update the position after we have an age */
          if (age == NULL)
            g_signal_connect_object (item,
                                     "notify::age",
                                     G_CALLBACK (on_notify_age_cb),
                                     self,
                                     G_CONNECT_SWAPPED);

          g_sequence_insert_sorted (self->seq,
                                    item,
                                    (GCompareDataFunc)_editor_sidebar_item_compare,
                                    NULL);
          self->length++;
        }
    }

  new_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));

  /* Notify of full change so we avoid doing the changes one-by-one
   * for larger result sets.
   */
  if (old_n_items || new_n_items)
    items_changed (self, 0, old_n_items, new_n_items);
}

static void
editor_sidebar_model_constructed (GObject *object)
{
  EditorSidebarModel *self = (EditorSidebarModel *)object;
  GPtrArray *pages;
  GArray *drafts;

  G_OBJECT_CLASS (editor_sidebar_model_parent_class)->constructed (object);

  if (self->session == NULL)
    {
      g_warning ("Attempt to create %s without a session!",
                 G_OBJECT_TYPE_NAME (self));
      return;
    }

  g_signal_connect_object (self->session,
                           "page-added",
                           G_CALLBACK (editor_sidebar_model_page_added_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->session,
                           "page-removed",
                           G_CALLBACK (editor_sidebar_model_page_removed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  pages = _editor_session_get_pages (self->session);

  for (guint i = 0; i < pages->len; i++)
    {
      EditorPage *page = g_ptr_array_index (pages, i);
      EditorWindow *window = _editor_page_get_window (page);

      editor_sidebar_model_page_added_cb (self, window, page, self->session);
    }

  drafts = _editor_session_get_drafts (self->session);

  for (guint i = 0; i < drafts->len; i++)
    {
      const EditorSessionDraft *draft = &g_array_index (drafts, EditorSessionDraft, i);
      g_autoptr(GFile) file = NULL;
      GSequenceIter *iter = NULL;

      if (draft->uri != NULL)
        {
          file = g_file_new_for_uri (draft->uri);
          iter = find_by_file (self, file);
        }

      if (iter == NULL)
        {
          g_autoptr(EditorSidebarItem) item = _editor_sidebar_item_new (file, NULL);
          g_autoptr(GFileInfo) info = NULL;
          guint position;

          _editor_sidebar_item_set_title (item, draft->title);
          _editor_sidebar_item_set_is_modified (item, TRUE, TRUE);
          _editor_sidebar_item_set_draft_id (item, draft->draft_id);

          /* For drafts, we're very likely on storage with cached, directory
           * information. Just query the mtime so we can get a reasonable
           * sort order on the draft.
           */
          if ((info = g_file_query_info (file,
                                         G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                         G_FILE_QUERY_INFO_NONE,
                                         NULL,
                                         NULL)))
            {
              g_autoptr(GDateTime) dt = g_file_info_get_modification_date_time (info);

              if (dt != NULL)
                _editor_sidebar_item_set_age (item, g_date_time_to_unix (dt));
            }

          iter = g_sequence_insert_sorted (self->seq,
                                           g_steal_pointer (&item),
                                           (GCompareDataFunc)_editor_sidebar_item_compare,
                                           NULL);
          position = g_sequence_iter_get_position (iter);

          items_changed (self, position, 0, 1);
        }
    }

  editor_session_load_recent_async (self->session,
                                    NULL,
                                    editor_sidebar_model_load_recent_cb,
                                    g_object_ref (self));
}

static void
editor_sidebar_model_finalize (GObject *object)
{
  EditorSidebarModel *self = (EditorSidebarModel *)object;

  g_clear_handle_id (&self->update_timer, g_source_remove);

  if (self->session)
    {
      g_signal_handlers_disconnect_by_func (self->session,
                                            G_CALLBACK (editor_sidebar_model_page_added_cb),
                                            self);
      g_signal_handlers_disconnect_by_func (self->session,
                                            G_CALLBACK (editor_sidebar_model_page_removed_cb),
                                            self);
      g_clear_weak_pointer (&self->session);
    }

  g_clear_pointer (&self->seq, g_sequence_free);

  G_OBJECT_CLASS (editor_sidebar_model_parent_class)->finalize (object);
}

static void
editor_sidebar_model_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  EditorSidebarModel *self = EDITOR_SIDEBAR_MODEL (object);

  switch (prop_id)
    {
    case PROP_N_ITEMS:
      g_value_set_uint (value, self->length);
      break;

    case PROP_SESSION:
      g_value_set_object (value, self->session);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_sidebar_model_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  EditorSidebarModel *self = EDITOR_SIDEBAR_MODEL (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_set_weak_pointer (&self->session, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_sidebar_model_class_init (EditorSidebarModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = editor_sidebar_model_constructed;
  object_class->finalize = editor_sidebar_model_finalize;
  object_class->get_property = editor_sidebar_model_get_property;
  object_class->set_property = editor_sidebar_model_set_property;

  properties [PROP_N_ITEMS] =
    g_param_spec_uint ("n-items", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SESSION] =
    g_param_spec_object ("session",
                         "Session",
                         "The EditorSession to be monitored",
                         EDITOR_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_sidebar_model_init (EditorSidebarModel *self)
{
  self->seq = g_sequence_new (g_object_unref);
  self->update_timer = g_timeout_add_seconds (60 * 5, update_timeout_cb, self);
}

EditorSidebarModel *
_editor_sidebar_model_new (EditorSession *session)
{
  return g_object_new (EDITOR_TYPE_SIDEBAR_MODEL,
                       "session", session,
                       NULL);
}

void
_editor_sidebar_model_page_reordered (EditorSidebarModel *self,
                                      EditorPage         *page,
                                      guint               page_num)
{
  g_autoptr(EditorSidebarItem) item = NULL;
  EditorDocument *document;
  GSequenceIter *iter;
  guint position;

  g_return_if_fail (EDITOR_IS_SIDEBAR_MODEL (self));
  g_return_if_fail (EDITOR_IS_PAGE (page));

  document = editor_page_get_document (page);

  /* We really *shouldn't* find it there as we don't track open
   * files in the sidebar any longer.
   */
  iter = find_by_document (self, document);
  if (iter == NULL)
    return;

  item = g_object_ref (g_sequence_get (iter));
  position = g_sequence_iter_get_position (iter);
  g_sequence_remove (iter);
  items_changed (self, position, 1, 0);

  iter = g_sequence_get_iter_at_pos (self->seq, page_num);
  g_sequence_insert_before (iter, g_steal_pointer (&item));
  items_changed (self, page_num, 0, 1);
}

void
_editor_sidebar_model_remove_document (EditorSidebarModel *self,
                                       EditorDocument     *document)
{
  GSequenceIter *iter;
  guint position;

  g_return_if_fail (EDITOR_IS_SIDEBAR_MODEL (self));
  g_return_if_fail (EDITOR_IS_DOCUMENT (document));

  iter = find_by_document (self, document);
  if (iter == NULL)
    return;

  position = g_sequence_iter_get_position (iter);
  g_sequence_remove (iter);
  items_changed (self, position, 1, 0);
}

void
_editor_sidebar_model_remove_draft (EditorSidebarModel *self,
                                    const gchar        *draft_id)
{
  GSequenceIter *iter;
  guint position;

  g_return_if_fail (EDITOR_IS_SIDEBAR_MODEL (self));
  g_return_if_fail (draft_id != NULL);

  iter = find_by_draft_id (self, draft_id);
  if (iter == NULL)
    return;

  position = g_sequence_iter_get_position (iter);
  g_sequence_remove (iter);
  items_changed (self, position, 1, 0);
}

void
_editor_sidebar_model_remove_file (EditorSidebarModel *self,
                                   GFile              *file)
{
  GSequenceIter *iter;
  guint position;

  g_return_if_fail (EDITOR_IS_SIDEBAR_MODEL (self));
  g_return_if_fail (G_IS_FILE (file));

  iter = find_by_file (self, file);
  if (iter == NULL)
    return;

  position = g_sequence_iter_get_position (iter);
  g_sequence_remove (iter);
  items_changed (self, position, 1, 0);
}
