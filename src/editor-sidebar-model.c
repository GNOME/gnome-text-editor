/* editor-sidebar-model.c
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

#define G_LOG_DOMAIN "editor-sidebar-model"

#include "config.h"

#include "editor-application.h"
#include "editor-document.h"
#include "editor-page-private.h"
#include "editor-session-private.h"
#include "editor-sidebar-item-private.h"
#include "editor-sidebar-model-private.h"
#include "editor-window.h"

struct _EditorSidebarModel
{
  GObject    parent_instance;
  GSequence *seq;
};

static guint
editor_sidebar_model_get_n_items (GListModel *model)
{
  EditorSidebarModel *self = EDITOR_SIDEBAR_MODEL (model);
  return g_sequence_get_length (self->seq);
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
  GSequenceIter *iter = g_sequence_get_iter_at_pos (self->seq, position);

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
find_by_document (EditorSidebarModel *self,
                  EditorDocument     *document)
{
  GSequenceIter *iter;

  g_assert (EDITOR_IS_SIDEBAR_MODEL (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  /* TODO: Once these are sorted, we can binary search, however
   *       we'll probably keep the open documents at the head
   *       and just linearly search them (but stop at NULL
   *       document).
   */

  for (iter = g_sequence_get_begin_iter (self->seq);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter))
    {
      EditorSidebarItem *item = g_sequence_get (iter);
      EditorPage *page = _editor_sidebar_item_get_page (item);

      if (page == NULL)
        continue;

      if (document == editor_page_get_document (page))
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
  GFile *file;

  g_assert (EDITOR_IS_SIDEBAR_MODEL (self));
  g_assert (EDITOR_IS_WINDOW (window));
  g_assert (EDITOR_IS_PAGE (page));
  g_assert (EDITOR_IS_SESSION (session));

  document = editor_page_get_document (page);
  file = editor_document_get_file (document);

  /* TODO: file vs draft, document, etc */

  iter = g_sequence_append (self->seq, _editor_sidebar_item_new (file, page));

  g_list_model_items_changed (G_LIST_MODEL (self),
                              g_sequence_iter_get_position (iter),
                              0,
                              1);
}

static void
editor_sidebar_model_page_removed_cb (EditorSidebarModel *self,
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

  if ((iter = find_by_document (self, document)))
    {
      guint position = g_sequence_iter_get_position (iter);
      g_sequence_remove (iter);
      g_list_model_items_changed (G_LIST_MODEL (self),
                                  position,
                                  1,
                                  0);
    }
}

static void
editor_sidebar_model_constructed (GObject *object)
{
  EditorSidebarModel *self = (EditorSidebarModel *)object;
  EditorSession *session;
  GPtrArray *pages;

  G_OBJECT_CLASS (editor_sidebar_model_parent_class)->constructed (object);

  session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);

  g_signal_connect_object (session,
                           "page-added",
                           G_CALLBACK (editor_sidebar_model_page_added_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (session,
                           "page-removed",
                           G_CALLBACK (editor_sidebar_model_page_removed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  pages = _editor_session_get_pages (session);

  for (guint i = 0; i < pages->len; i++)
    {
      EditorPage *page = g_ptr_array_index (pages, i);
      EditorWindow *window = _editor_page_get_window (page);

      editor_sidebar_model_page_added_cb (self, window, page, session);
    }
}

static void
editor_sidebar_model_finalize (GObject *object)
{
  EditorSidebarModel *self = (EditorSidebarModel *)object;

  g_clear_pointer (&self->seq, g_sequence_free);

  G_OBJECT_CLASS (editor_sidebar_model_parent_class)->finalize (object);
}

static void
editor_sidebar_model_class_init (EditorSidebarModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = editor_sidebar_model_constructed;
  object_class->finalize = editor_sidebar_model_finalize;
}

static void
editor_sidebar_model_init (EditorSidebarModel *self)
{
  self->seq = g_sequence_new (g_object_unref);
}

EditorSidebarModel *
_editor_sidebar_model_new (void)
{
  return g_object_new (EDITOR_TYPE_SIDEBAR_MODEL, NULL);
}
