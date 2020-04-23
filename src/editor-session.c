/* editor-session.c
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

#define G_LOG_DOMAIN "editor-session"

#include "config.h"

#include "editor-application.h"
#include "editor-document-private.h"
#include "editor-page-private.h"
#include "editor-session-private.h"
#include "editor-window-private.h"

#define AUTO_SAVE_TIMEOUT_SECONDS 20

struct _EditorSession
{
  GObject     parent_instance;
  GPtrArray  *windows;
  GPtrArray  *pages;
  GFile      *state_file;
  GHashTable *seen;
  GArray     *drafts;

  guint      auto_save_source;

  guint      auto_save : 1;
  guint      did_restore : 1;
};

typedef struct
{
  GApplication *app;
  GFile        *state_file;
  GBytes       *state_bytes;
  GPtrArray    *seen;
  guint         n_active;
} EditorSessionSave;

typedef struct
{
  guint32 line;
  guint32 line_offset;
} Position;

typedef struct
{
  Position begin;
  Position end;
} Selection;

G_DEFINE_TYPE (EditorSession, editor_session, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_AUTO_SAVE,
  N_PROPS
};

enum {
  PAGE_ADDED,
  PAGE_REMOVED,
  WINDOW_ADDED,
  WINDOW_REMOVED,
  N_SIGNALS
};

static GParamSpec *properties [N_PROPS];
static guint signals[N_SIGNALS];

static void
selection_free (Selection *selection)
{
  g_slice_free (Selection, selection);
}

static void
clear_draft (EditorSessionDraft *draft)
{
  g_clear_pointer (&draft->draft_id, g_free);
  g_clear_pointer (&draft->title, g_free);
  g_clear_pointer (&draft->uri, g_free);
}

static GVariant *
selection_to_variant (Selection *selection)
{
  G_STATIC_ASSERT (sizeof (Selection) == sizeof (guint32) * 4);

  return g_variant_new_fixed_array (G_VARIANT_TYPE ("(uu)"),
                                    selection,
                                    2,
                                    sizeof (Position));
}

static gboolean
selection_from_variant (Selection *selection,
                        GVariant  *variant)
{
  if (g_variant_is_of_type (variant, G_VARIANT_TYPE ("a(uu)")) &&
      g_variant_n_children (variant) == 2)
    {
      GVariantIter iter;

      g_variant_iter_init (&iter, variant);
      g_variant_iter_next (&iter, "(uu)", &selection->begin.line, &selection->begin.line_offset);
      g_variant_iter_next (&iter, "(uu)", &selection->end.line, &selection->end.line_offset);

      return TRUE;
    }

  return FALSE;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Selection, selection_free);

static gchar *
get_bookmarks_filename (void)
{
  return g_build_filename (g_get_user_data_dir (),
                           APP_ID,
                           "recently-used.xbel",
                           NULL);
}

static void
editor_session_save_free (EditorSessionSave *state)
{
  g_clear_pointer (&state->state_bytes, g_bytes_unref);
  g_clear_object (&state->state_file);
  g_clear_pointer (&state->app, g_application_release);
  g_clear_pointer (&state->seen, g_ptr_array_unref);
  g_slice_free (EditorSessionSave, state);
}

static void
add_draft_state (EditorSession   *self,
                 GVariantBuilder *builder)
{
  g_assert (EDITOR_IS_SESSION (self));
  g_assert (builder != NULL);

  g_variant_builder_open (builder, G_VARIANT_TYPE ("{sv}"));
  g_variant_builder_add (builder, "s", "drafts");
  g_variant_builder_open (builder, G_VARIANT_TYPE ("v"));
  g_variant_builder_open (builder, G_VARIANT_TYPE ("aa{sv}"));
  for (guint i = 0; i < self->drafts->len; i++)
    {
      const EditorSessionDraft *draft = &g_array_index (self->drafts, EditorSessionDraft, i);

      g_variant_builder_open (builder, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add_parsed (builder, "{'draft-id', <%s>}", draft->draft_id);
      if (draft->title != NULL)
        g_variant_builder_add_parsed (builder, "{'title', <%s>}", draft->title);
      if (draft->uri != NULL)
        g_variant_builder_add_parsed (builder, "{'uri', <%s>}", draft->uri);
      g_variant_builder_close (builder);
    }
  g_variant_builder_close (builder);
  g_variant_builder_close (builder);
  g_variant_builder_close (builder);
}

static void
add_window_state (EditorSession   *self,
                  GVariantBuilder *builder)
{
  g_assert (EDITOR_IS_SESSION (self));
  g_assert (builder != NULL);

  g_variant_builder_open (builder, G_VARIANT_TYPE ("{sv}"));
  g_variant_builder_add (builder, "s", "windows");
  g_variant_builder_open (builder, G_VARIANT_TYPE ("v"));
  g_variant_builder_open (builder, G_VARIANT_TYPE ("aa{sv}"));

  for (guint i = 0; i < self->windows->len; i++)
    {
      EditorWindow *window = g_ptr_array_index (self->windows, i);
      const GList *pages = _editor_window_get_pages (window);
      gboolean is_active = gtk_window_is_active (GTK_WINDOW (window));
      gint width, height;

      g_variant_builder_open (builder, G_VARIANT_TYPE ("a{sv}"));

      /* Track if this should be the foreground window */
      if (is_active)
        g_variant_builder_add_parsed (builder, "{'is-active', <%b>}", is_active);

      if (_editor_window_get_sidebar_revealed (window))
        g_variant_builder_add_parsed (builder, "{'sidebar-revealed', <%b>}", TRUE);

      /* Store the window size */
      gtk_window_get_size (GTK_WINDOW (window), &width, &height);
      g_variant_builder_add_parsed (builder,
                                    "{'size', <(%u,%u)>}",
                                    CLAMP (width, 0, 10000),
                                    CLAMP (height, 0, 10000));

      /* Add all of the pages from the window */
      g_variant_builder_open (builder, G_VARIANT_TYPE ("{sv}"));
      g_variant_builder_add (builder, "s", "pages");
      g_variant_builder_open (builder, G_VARIANT_TYPE ("v"));
      g_variant_builder_open (builder, G_VARIANT_TYPE ("aa{sv}"));
      for (const GList *iter = pages; iter; iter = iter->next)
        {
          EditorPage *page = iter->data;
          EditorDocument *document = editor_page_get_document (page);
          GtkSourceLanguage *language = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (document));
          GFile *file = editor_document_get_file (document);
          const gchar *draft_id = _editor_document_get_draft_id (document);
          gboolean page_is_active = editor_page_is_active (page);
          GtkTextMark *insert = gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (document));
          GtkTextMark *bound = gtk_text_buffer_get_selection_bound (GTK_TEXT_BUFFER (document));
          GtkTextIter begin, end;
          Selection sel;

          /* If this is a draft (meaning no backing file has been set) and
           * there are no modifications, we should ignore this page as we don't
           * want to restore it.
           */
          if (editor_page_get_can_discard (page))
            continue;

          gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (document), &begin, insert);
          gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (document), &end, bound);

          sel.begin.line = gtk_text_iter_get_line (&begin);
          sel.begin.line_offset = gtk_text_iter_get_line_offset (&begin);
          sel.end.line = gtk_text_iter_get_line (&end);
          sel.end.line_offset = gtk_text_iter_get_line_offset (&end);

          g_variant_builder_open (builder, G_VARIANT_TYPE ("a{sv}"));
          g_variant_builder_add_parsed (builder, "{'draft-id', <%s>}", draft_id);
          if (language != NULL)
            g_variant_builder_add_parsed (builder,
                                          "{'language', <%s>}",
                                          gtk_source_language_get_id (language));
          g_variant_builder_add (builder, "{sv}", "selection", selection_to_variant (&sel));
          if (page_is_active)
            g_variant_builder_add_parsed (builder, "{'is-active', <%b>}", page_is_active);
          if (file != NULL)
            {
              g_autofree gchar *uri = g_file_get_uri (file);
              g_variant_builder_add_parsed (builder, "{'uri', <%s>}", uri);
            }
          g_variant_builder_close (builder);
        }
      g_variant_builder_close (builder);
      g_variant_builder_close (builder);
      g_variant_builder_close (builder);

      g_variant_builder_close (builder);
    }

  g_variant_builder_close (builder);
  g_variant_builder_close (builder);
  g_variant_builder_close (builder);
}

static EditorWindow *
find_or_create_window (EditorSession *self)
{
  GtkApplication *app = GTK_APPLICATION (EDITOR_APPLICATION_DEFAULT);
  const GList *windows = gtk_application_get_windows (app);
  EditorWindow *window;

  g_assert (EDITOR_IS_SESSION (self));
  g_assert (EDITOR_IS_APPLICATION (app));

  /* Try to find the most recent editor window displayed */
  for (const GList *iter = windows; iter; iter = iter->next)
    {
      GtkWindow *win = iter->data;

      g_assert (GTK_IS_WINDOW (win));

      if (EDITOR_IS_WINDOW (win))
        return EDITOR_WINDOW (win);
    }

  /* Create our first editor window if necessary */
  window = _editor_window_new ();
  editor_session_add_window (self, window);
  return window;
}

static EditorPage *
find_page_for_file (EditorSession *self,
                    GFile         *file)
{
  g_assert (EDITOR_IS_SESSION (self));
  g_assert (G_IS_FILE (file));

  for (guint i = 0; i < self->pages->len; i++)
    {
      EditorPage *page = g_ptr_array_index (self->pages, i);
      EditorDocument *document = editor_page_get_document (page);
      GFile *cur = editor_document_get_file (document);

      if (cur != NULL && g_file_equal (cur, file))
        return page;
    }

  return NULL;
}

static void
editor_session_dispose (GObject *object)
{
  EditorSession *self = (EditorSession *)object;

  g_assert (EDITOR_IS_SESSION (self));

  g_clear_handle_id (&self->auto_save_source, g_source_remove);

  if (self->windows->len > 0)
    g_ptr_array_remove_range (self->windows, 0, self->windows->len);

  if (self->pages->len > 0)
    g_ptr_array_remove_range (self->pages, 0, self->pages->len);

  g_hash_table_remove_all (self->seen);

  G_OBJECT_CLASS (editor_session_parent_class)->dispose (object);
}

static void
editor_session_finalize (GObject *object)
{
  EditorSession *self = (EditorSession *)object;

  g_clear_pointer (&self->pages, g_ptr_array_unref);
  g_clear_pointer (&self->windows, g_ptr_array_unref);
  g_clear_pointer (&self->seen, g_hash_table_unref);
  g_clear_pointer (&self->drafts, g_array_unref);
  g_clear_object (&self->state_file);

  G_OBJECT_CLASS (editor_session_parent_class)->finalize (object);
}

static void
editor_session_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  EditorSession *self = EDITOR_SESSION (object);

  switch (prop_id)
    {
    case PROP_AUTO_SAVE:
      g_value_set_boolean (value, editor_session_get_auto_save (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_session_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  EditorSession *self = EDITOR_SESSION (object);

  switch (prop_id)
    {
    case PROP_AUTO_SAVE:
      editor_session_set_auto_save (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_session_class_init (EditorSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_session_dispose;
  object_class->finalize = editor_session_finalize;
  object_class->get_property = editor_session_get_property;
  object_class->set_property = editor_session_set_property;

  /**
   * EditorSession:auto-save:
   *
   * The "auto-save" property denotes that the session should auto
   * save it's state on a regular interval to ensure that little to
   * no state is lost in the case of an application crash.
   *
   * Documents are saved to their draft files so enabling this option
   * is safe in terms of mutating the original files as they are left
   * untouched.
   */
  properties [PROP_AUTO_SAVE] =
    g_param_spec_boolean ("auto-save",
                          "Auto Save",
                          "Auto Save",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * EditorSession::page-added:
   * @self: an #EditorSession
   * @window: an #EditorWindow
   * @page: an #EditorPage
   *
   * The "page-added" signal is emitted when a new page is added to an
   * #EditorWindow.
   */
  signals [PAGE_ADDED] =
    g_signal_new ("page-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2,
                  EDITOR_TYPE_WINDOW,
                  EDITOR_TYPE_PAGE);

  /**
   * EditorSession::page-removed:
   * @self: an #EditorSession
   * @window: an #EditorWindow
   * @page: an #EditorPage
   *
   * The "page-removed" signal is emitted when a page has been removed
   * from an #EditorWindow.
   */
  signals [PAGE_REMOVED] =
    g_signal_new ("page-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2,
                  EDITOR_TYPE_WINDOW,
                  EDITOR_TYPE_PAGE);

  /**
   * EditorSession::window-added:
   * @self: an #EditorSession
   * @window: an #EditorWindow
   *
   * The "window-added" signal is emitted when a new #EditorWindow
   * is created.
   */
  signals [WINDOW_ADDED] =
    g_signal_new ("window-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  EDITOR_TYPE_WINDOW);

  /**
   * EditorSession::window-removed:
   * @self: an #EditorSession
   * @window: an #EditorWindow
   *
   * The "window-removed" signal is emitted when a new #EditorWindow
   * is removed.
   */
  signals [WINDOW_REMOVED] =
    g_signal_new ("window-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  EDITOR_TYPE_WINDOW);
}

static void
editor_session_init (EditorSession *self)
{
  self->seen = g_hash_table_new_full ((GHashFunc) g_file_hash,
                                      (GEqualFunc) g_file_equal,
                                      g_object_unref, NULL);
  self->pages = g_ptr_array_new_with_free_func (g_object_unref);
  self->windows = g_ptr_array_new_with_free_func (g_object_unref);
  self->state_file = g_file_new_build_filename (g_get_user_data_dir (),
                                                APP_ID,
                                                "session.gvariant",
                                                NULL);
  self->drafts = g_array_new (FALSE, FALSE, sizeof (EditorSessionDraft));
  g_array_set_clear_func (self->drafts, (GDestroyNotify) clear_draft);
}

EditorSession *
_editor_session_new (void)
{
  return g_object_new (EDITOR_TYPE_SESSION, NULL);
}

/**
 * editor_session_add_window:
 * @self: an #EditorSession
 *
 * Adds a new window to the session.
 */
void
editor_session_add_window (EditorSession *self,
                           EditorWindow  *window)
{
  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (EDITOR_IS_WINDOW (window));

  g_ptr_array_add (self->windows, g_object_ref (window));

  g_signal_emit (self, signals [WINDOW_ADDED], 0, window);
}

EditorWindow *
_editor_session_create_window_no_draft (EditorSession *self)
{
  g_autoptr(EditorDocument) document = NULL;
  EditorWindow *window;

  g_return_val_if_fail (EDITOR_IS_SESSION (self), NULL);

  window = _editor_window_new ();
  editor_session_add_window (self, window);

  return window;
}

/**
 * editor_session_create_window:
 * @self: an #EditorSession
 *
 * Creates a new #EditorWindow and registers it with the #EditorSession.
 * The window is presented with a new draft displayed.
 *
 * Returns: (transfer none): an #EditorWindow
 */
EditorWindow *
editor_session_create_window (EditorSession *self)
{
  g_autoptr(EditorDocument) document = NULL;
  EditorWindow *window;

  g_return_val_if_fail (EDITOR_IS_SESSION (self), NULL);

  window = _editor_window_new ();
  editor_session_add_window (self, window);

  document = editor_document_new_draft ();
  editor_session_add_document (self, window, document);

  gtk_window_present (GTK_WINDOW (window));

  return window;
}

/**
 * editor_session_add_page:
 * @self: an #EditorSession
 * @window: (nullable): an #EditorWindow or %NULL
 * @page: an #EditorPage
 *
 * Adds @page to @window.
 *
 * If @window is %NULL, then a new window is created.
 *
 * The window will be presented and raised as part of this operation.
 *
 * Returns:
 */
void
editor_session_add_page (EditorSession *self,
                         EditorWindow  *window,
                         EditorPage    *page)
{
  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (EDITOR_IS_WINDOW (window));
  g_return_if_fail (EDITOR_IS_PAGE (page));

  if (window == NULL)
    window = find_or_create_window (self);

  g_assert (window != NULL);
  g_assert (EDITOR_IS_WINDOW (window));

  g_ptr_array_add (self->pages, g_object_ref (page));

  _editor_window_add_page (window, page);
  _editor_page_raise (page);

  gtk_window_present (GTK_WINDOW (window));

  gtk_widget_grab_focus (GTK_WIDGET (page));

  g_signal_emit (self, signals [PAGE_ADDED], 0, window, page);
}

/**
 * editor_session_add_document:
 * @self: an #EditorSession
 * @window: (nullable): an #EditorWindow or %NULL
 * @document: an #EditorDocument
 *
 * Adds @document to @window after creating a new #EditorPage.
 *
 * If @window is %NULL, then a new window is created.
 *
 * The window will be presented and raised as part of this operation.
 */
void
editor_session_add_document (EditorSession  *self,
                             EditorWindow   *window,
                             EditorDocument *document)
{
  EditorPage *page;

  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (!window || EDITOR_IS_WINDOW (window));
  g_return_if_fail (EDITOR_IS_DOCUMENT (document));

  if (window == NULL)
    window = find_or_create_window (self);

  page = editor_page_new_for_document (document);
  editor_session_add_page (self, window, page);
}

void
editor_session_add_draft (EditorSession *self,
                          EditorWindow  *window)
{
  g_autoptr(EditorDocument) draft = NULL;

  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (!window || EDITOR_IS_WINDOW (window));

  draft = editor_document_new_draft ();
  editor_session_add_document (self, window, draft);
}

static void
editor_session_remove_page_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  EditorDocument *document = (EditorDocument *)object;
  g_autoptr(EditorPage) page = user_data;
  g_autoptr(GError) error = NULL;
  EditorWindow *window;

  g_assert (EDITOR_IS_DOCUMENT (document));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (EDITOR_IS_PAGE (page));

  if (!_editor_document_save_draft_finish (document, result, &error))
    g_warning ("Failed to save draft: %s", error->message);

  if ((window = _editor_page_get_window (page)))
    _editor_window_remove_page (window, page);
}

static void
editor_session_stash_draft (EditorSession *self,
                            const gchar   *draft_id,
                            const gchar   *title,
                            GFile         *file)
{
  g_autofree gchar *draft_uri = NULL;
  EditorSessionDraft d;

  g_assert (EDITOR_IS_SESSION (self));
  g_assert (draft_id != NULL);
  g_assert (!file || G_IS_FILE (file));

  for (guint i = 0; i < self->drafts->len; i++)
    {
      EditorSessionDraft *draft = &g_array_index (self->drafts, EditorSessionDraft, i);
      g_autofree gchar *uri = NULL;

      if (g_strcmp0 (draft->draft_id, draft_id) != 0)
        continue;

      if (g_strcmp0 (draft->title, title) != 0)
        {
          g_clear_pointer (&draft->title, g_free);
          draft->title = g_strdup (title);
        }

      if (file != NULL)
        uri = g_file_get_uri (file);

      if (g_strcmp0 (draft->uri, uri) != 0)
        {
          g_clear_pointer (&draft->uri, g_free);
          draft->uri = g_steal_pointer (&uri);
        }

      return;
    }

  if (file != NULL)
    draft_uri = g_file_get_uri (file);

  d.title = g_strdup (title);
  d.uri = g_steal_pointer (&draft_uri);
  d.draft_id = g_strdup (draft_id);

  g_array_append_val (self->drafts, d);
}

/**
 * editor_session_remove_page:
 * @self: an #EditorSession
 * @page: an #EditorPage
 *
 * Removes a @page from it's window and unloads all of the
 * documents within the window.
 */
void
editor_session_remove_page (EditorSession *self,
                            EditorPage    *page)
{
  EditorDocument *document;
  EditorWindow *window;

  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (EDITOR_IS_PAGE (page));

  document = editor_page_get_document (page);
  window = _editor_page_get_window (page);

  g_return_if_fail (EDITOR_IS_DOCUMENT (document));
  g_return_if_fail (EDITOR_IS_WINDOW (window));

  g_object_ref (page);

  if (g_ptr_array_remove (self->pages, page))
    {
      /* If this page contains modifications, we don't want
       * to lose them when the page is removed. We want to
       * keep track that there is modified state (and where
       * to restore it from) so when the user clicks on it
       * from the sidebar, the state is reloaded.
       */
      if (editor_page_get_is_modified (page))
        {
          g_autofree gchar *title = _editor_page_dup_title_no_i18n (page);
          const gchar *draft_id = _editor_document_get_draft_id (document);
          GFile *file = editor_document_get_file (document);

          editor_session_stash_draft (self, draft_id, title, file);
        }

      g_signal_emit (self, signals [PAGE_REMOVED], 0, window, page);
    }

  _editor_document_save_draft_async (document,
                                     NULL,
                                     editor_session_remove_page_cb,
                                     g_object_ref (page));

  g_object_unref (page);
}

/**
 * editor_session_remove_document:
 * @self: an #EditorSession
 * @document: an #EditorDocument
 *
 * Removes @document from the #EditorSession.
 */
void
editor_session_remove_document (EditorSession  *self,
                                EditorDocument *document)
{
  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (EDITOR_IS_DOCUMENT (document));

  /* Iterate backwards to be safe from removal */
  for (guint i = self->pages->len; i > 0; i--)
    {
      EditorPage *page = g_ptr_array_index (self->pages, i - 1);
      EditorDocument *page_document = editor_page_get_document (page);

      if (document == page_document)
        editor_session_remove_page (self, page);
    }
}

/**
 * editor_session_remove_window:
 * @self: an #EditorSession
 * @window: an #EditorWindow
 *
 * Removes @window from the session after removing all of the
 * pages from the window.
 */
void
editor_session_remove_window (EditorSession *self,
                              EditorWindow  *window)
{
  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (EDITOR_IS_WINDOW (window));

  /* If this is the last window, then we want to save the session
   * so that we can restore it next time we start.
   */
  if (self->windows->len == 1 &&
      EDITOR_WINDOW (g_ptr_array_index (self->windows, 0)) == window)
    {
      editor_session_save_async (self, NULL, NULL, NULL);
      gtk_widget_destroy (GTK_WIDGET (window));
      return;
    }

  g_object_ref (window);

  if (g_ptr_array_remove (self->windows, window))
    {
      GList *pages = _editor_window_get_pages (window);

      for (const GList *iter = pages; iter; iter = iter->next)
        {
          EditorPage *page = iter->data;

          editor_session_remove_page (self, page);
        }

      g_list_free (pages);

      g_signal_emit (self, signals [WINDOW_REMOVED], 0, window);

      gtk_widget_destroy (GTK_WIDGET (window));
    }

  g_object_unref (window);
}

static void
editor_session_update_recent_worker (GTask        *task,
                                     gpointer      source_object,
                                     gpointer      task_data,
                                     GCancellable *cancellable)
{
  EditorSessionSave *save = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (EDITOR_IS_SESSION (source_object));
  g_assert (save != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (save->seen != NULL)
    {
      g_autoptr(GBookmarkFile) bookmarks = g_bookmark_file_new ();
      g_autofree gchar *filename = get_bookmarks_filename ();
      g_autoptr(GError) error = NULL;

      if (!g_bookmark_file_load_from_file (bookmarks, filename, &error))
        {
          if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            g_warning ("Failed to load bookmarks file: %s", error->message);
          g_clear_error (&error);
        }

      for (guint i = 0; i < save->seen->len; i++)
        {
          GFile *file = g_ptr_array_index (save->seen, i);
          g_autofree gchar *uri = g_file_get_uri (file);

          g_assert (G_IS_FILE (file));

          g_bookmark_file_add_application (bookmarks, uri, NULL, NULL);
        }

      if (!g_bookmark_file_to_file (bookmarks, filename, &error))
        {
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }
    }

  g_task_return_boolean (task, TRUE);
}

static void
editor_session_save_unlink_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!g_file_delete_finish (file, result, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Failed to unlink session file: %s",
                   error->message);
    }

  g_task_run_in_thread (task, editor_session_update_recent_worker);
}

static void
editor_session_save_replace_contents_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!g_file_replace_contents_finish (file, result, NULL, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_run_in_thread (task, editor_session_update_recent_worker);
}

static void
editor_session_save_draft_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  EditorDocument *document = (EditorDocument *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  EditorSessionSave *state;

  g_assert (EDITOR_IS_DOCUMENT (document));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!_editor_document_save_draft_finish (document, result, &error))
    g_warning ("Failed to save draft: %s", error->message);

  state = g_task_get_task_data (task);
  state->n_active--;

  if (state->n_active == 0)
    g_file_replace_contents_bytes_async (state->state_file,
                                         state->state_bytes,
                                         NULL,
                                         FALSE,
                                         G_FILE_CREATE_REPLACE_DESTINATION,
                                         NULL,
                                         editor_session_save_replace_contents_cb,
                                         g_steal_pointer (&task));
}

void
editor_session_save_async (EditorSession       *self,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  g_autoptr(GVariant) vstate = NULL;
  g_autoptr(GTask) task = NULL;
  g_autofree gchar *drafts_dir = NULL;
  EditorSessionSave *state;
  GVariantBuilder builder;

  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add_parsed (&builder, "{'version', <%u>}", 1);
  add_draft_state (self, &builder);
  add_window_state (self, &builder);
  vstate = g_variant_builder_end (&builder);

  state = g_slice_new0 (EditorSessionSave);
  state->state_file = g_file_dup (self->state_file);
  state->state_bytes = g_variant_get_data_as_bytes (vstate);
  state->app = g_application_get_default ();

  if (g_hash_table_size (self->seen) > 0)
    {
      GHashTableIter iter;
      GFile *file;

      state->seen = g_ptr_array_new_with_free_func (g_object_unref);

      g_hash_table_iter_init (&iter, self->seen);
      while (g_hash_table_iter_next (&iter, (gpointer *)&file, NULL))
        g_ptr_array_add (state->seen, g_file_dup (file));
    }

  g_application_hold (state->app);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, editor_session_save_async);
  g_task_set_task_data (task, state, (GDestroyNotify) editor_session_save_free);

  drafts_dir = g_build_filename (g_get_user_data_dir (), APP_ID, "drafts", NULL);
  g_mkdir_with_parents (drafts_dir, 0750);

  /* First we need to make every page uneditable so that no changes
   * can be made while we save the state to disk.
   */
  for (guint i = 0; i < self->pages->len; i++)
    {
      EditorPage *page = g_ptr_array_index (self->pages, i);
      EditorDocument *document = editor_page_get_document (page);

      g_assert (EDITOR_IS_PAGE (page));
      g_assert (EDITOR_IS_DOCUMENT (document));

      if (editor_page_get_can_discard (page))
        continue;

      state->n_active++;

      _editor_document_save_draft_async (document,
                                         NULL,
                                         editor_session_save_draft_cb,
                                         g_object_ref (task));
    }

  /* If we haven't started any draft saving, then we have no
   * operations to restore and can just unlink the session file.
   */
  if (state->n_active == 0)
    {
      if (state->seen == NULL)
        g_file_delete_async (self->state_file,
                             G_PRIORITY_HIGH,
                             NULL,
                             editor_session_save_unlink_cb,
                             g_steal_pointer (&task));
      else
        g_file_replace_contents_bytes_async (state->state_file,
                                             state->state_bytes,
                                             NULL,
                                             FALSE,
                                             G_FILE_CREATE_REPLACE_DESTINATION,
                                             NULL,
                                             editor_session_save_replace_contents_cb,
                                             g_steal_pointer (&task));
    }
}

gboolean
editor_session_save_finish (EditorSession  *self,
                            GAsyncResult   *result,
                            GError        **error)
{
  g_return_val_if_fail (EDITOR_IS_SESSION (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static const gchar *
get_draft_id_for_file (EditorSession *self,
                       GFile         *file)
{
  g_autofree gchar *uri = NULL;

  g_assert (EDITOR_IS_SESSION (self));
  g_assert (!file || G_IS_FILE (file));

  if (file == NULL)
    return NULL;

  uri = g_file_get_uri (file);

  for (guint i = 0; i < self->drafts->len; i++)
    {
      const EditorSessionDraft *draft = &g_array_index (self->drafts, EditorSessionDraft, i);

      if (g_strcmp0 (uri, draft->uri) == 0)
        return draft->draft_id;
    }

  return NULL;
}

void
editor_session_open (EditorSession *self,
                     EditorWindow  *window,
                     GFile         *file)
{
  g_autoptr(EditorDocument) document = NULL;
  g_autofree gchar *uri = NULL;
  const gchar *draft_id;
  EditorPage *remove = NULL;
  EditorPage *page;

  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (!window || EDITOR_IS_WINDOW (window));
  g_return_if_fail (G_IS_FILE (file));

  uri = g_file_get_uri (file);
  g_debug ("Attempting to open file: \"%s\"", uri);

  if ((page = find_page_for_file (self, file)))
    {
      _editor_page_raise (page);
      return;
    }

  if (window == NULL)
    window = find_or_create_window (self);

  if ((page = editor_window_get_visible_page (window)) &&
      editor_page_get_can_discard (page))
    remove = page;

  document = editor_document_new_for_file (file);

  if ((draft_id = get_draft_id_for_file (self, file)))
    _editor_document_set_draft_id (document, draft_id);

  page = editor_page_new_for_document (document);
  editor_session_add_page (self, window, page);

  if (remove)
    editor_session_remove_page (self, remove);

  _editor_document_load_async (document, window, NULL, NULL, NULL);
}

void
editor_session_open_files (EditorSession  *self,
                           GFile         **files,
                           gint            n_files)
{
  g_return_if_fail (EDITOR_IS_SESSION (self));

  for (guint i = 0; i < n_files; i++)
    editor_session_open (self, NULL, files[i]);
}

void
_editor_session_open_draft (EditorSession *self,
                            EditorWindow  *window,
                            const gchar   *draft_id)
{
  g_autoptr(EditorDocument) new_document = NULL;
  EditorPage *remove = NULL;
  EditorPage *visible_page;

  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (!window || EDITOR_IS_WINDOW (window));
  g_return_if_fail (draft_id != NULL);

  g_debug ("Attempting to open draft \"%s\"", draft_id);

  if (window == NULL)
    window = find_or_create_window (self);

  if ((visible_page = editor_window_get_visible_page (window)) &&
      editor_page_get_can_discard (visible_page))
    remove = visible_page;

  for (guint i = 0; i < self->pages->len; i++)
    {
      EditorPage *page = g_ptr_array_index (self->pages, i);
      EditorDocument *document = editor_page_get_document (page);
      const gchar *doc_draft_id = _editor_document_get_draft_id (document);

      /* If this document matches the draft-id, short-circuit. */
      if (g_strcmp0 (doc_draft_id, draft_id) == 0)
        {
          _editor_page_raise (page);
          return;
        }
    }

  new_document = _editor_document_new (NULL, draft_id);
  editor_session_add_document (self, window, new_document);
  _editor_document_load_async (new_document, window, NULL, NULL, NULL);

  if (remove)
    editor_session_remove_page (self, remove);
}

static void
editor_session_load_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  EditorDocument *document = (EditorDocument *)object;
  g_autoptr(Selection) sel = user_data;
  g_autoptr(GError) error = NULL;
  GtkTextIter begin, end;

  g_assert (EDITOR_IS_DOCUMENT (document));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (sel != NULL);

  if (!_editor_document_load_finish (document, result, &error))
    g_warning ("Failed to load document: %s", error->message);

  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document),
                                           &begin,
                                           sel->begin.line,
                                           sel->begin.line_offset);
  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document),
                                           &end,
                                           sel->end.line,
                                           sel->end.line_offset);
  gtk_text_buffer_select_range (GTK_TEXT_BUFFER (document), &begin, &end);
}

static void
delete_unused_worker (GTask        *task,
                      gpointer      source_object,
                      gpointer      task_data,
                      GCancellable *cancellable)
{
  g_autoptr(GFileEnumerator) enumerator = NULL;
  g_autoptr(GError) error = NULL;
  const gchar * const *files = task_data;
  GFile *file = source_object;
  gpointer infoptr;

  g_assert (G_IS_TASK (task));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_assert (G_IS_FILE (file));
  g_assert (files != NULL);

  enumerator = g_file_enumerate_children (file,
                                          G_FILE_ATTRIBUTE_STANDARD_NAME","
                                          G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL,
                                          &error);

  if (enumerator == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  while ((infoptr = g_file_enumerator_next_file (enumerator, NULL, NULL)))
    {
      g_autoptr(GFileInfo) info = infoptr;
      const gchar *name = g_file_info_get_name (info);

      if (!g_strv_contains (files, name))
        {
          g_autoptr(GFile) child = g_file_enumerator_get_child (enumerator, info);

          if (!g_file_delete (child, NULL, &error))
            {
              g_warning ("Failed to remove draft \"%s\": %s",
                         name, error->message);
              g_clear_error (&error);
            }
        }
    }

  g_task_return_boolean (task, TRUE);
}

static void
editor_session_restore_delete_unused (EditorSession *self)
{
  g_autoptr(GPtrArray) ar = NULL;
  g_autoptr(GFile) drafts_dir = NULL;
  g_autoptr(GTask) task = NULL;

  g_assert (EDITOR_IS_SESSION (self));

  ar = g_ptr_array_new_with_free_func (g_free);

  /* Add draft_id from known pages */
  for (guint i = 0; i < self->pages->len; i++)
    {
      EditorPage *page = g_ptr_array_index (self->pages, i);
      EditorDocument *document = editor_page_get_document (page);
      const gchar *draft_id = _editor_document_get_draft_id (document);

      g_ptr_array_add (ar, g_strdup (draft_id));
    }

  /* Now add draft_id from unopened but known drafts */
  for (guint i = 0; i < self->drafts->len; i++)
    {
      const EditorSessionDraft *draft = &g_array_index (self->drafts, EditorSessionDraft, i);

      g_ptr_array_add (ar, g_strdup (draft->draft_id));
    }

  g_ptr_array_add (ar, NULL);

  drafts_dir = g_file_new_build_filename (g_get_user_data_dir (),
                                          APP_ID,
                                          "drafts",
                                          NULL);

  task = g_task_new (drafts_dir, NULL, NULL, NULL);
  g_task_set_task_data (task,
                        g_ptr_array_free (g_steal_pointer (&ar), FALSE),
                        (GDestroyNotify) g_strfreev);
  g_task_run_in_thread (task, delete_unused_worker);
}

static gboolean
editor_session_restore_v1_pages (EditorSession *self,
                                 EditorWindow  *window,
                                 GVariant      *pages)
{
  g_autoptr(GVariant) page = NULL;
  EditorPage *active = NULL;
  GVariantIter iter;

  g_assert (EDITOR_IS_SESSION (self));
  g_assert (EDITOR_IS_WINDOW (window));
  g_assert (pages != NULL);
  g_assert (g_variant_is_of_type (pages, G_VARIANT_TYPE ("aa{sv}")));

  g_variant_iter_init (&iter, pages);
  while (g_variant_iter_loop (&iter, "@a{sv}", &page))
    {
      g_autoptr(EditorDocument) document = NULL;
      g_autoptr(GVariant) sel_value = NULL;
      g_autoptr(GFile) file = NULL;
      EditorPage *epage = NULL;
      const gchar *draft_id;
      const gchar *uri;
      gboolean is_active;
      Selection sel;

      if (!g_variant_lookup (page, "draft-id", "&s", &draft_id))
        draft_id = NULL;

      if (!g_variant_lookup (page, "uri", "&s", &uri))
        uri = NULL;

      if (!(sel_value = g_variant_lookup_value (page, "selection", NULL)) ||
          !selection_from_variant (&sel, sel_value))
        {
          sel.begin.line = 0;
          sel.begin.line_offset = 0;
          sel.end.line = 0;
          sel.end.line_offset = 0;
        }

      if (!g_variant_lookup (page, "is-active", "b", &is_active))
        is_active = FALSE;

      /* We need either a draft-id or file URI to load */
      if (uri == NULL && draft_id == NULL)
        continue;

      if (uri != NULL)
        file = g_file_new_for_uri (uri);

      document = _editor_document_new (file, draft_id);
      epage = editor_page_new_for_document (document);
      editor_session_add_page (self, window, epage);

      _editor_document_load_async (document,
                                   window,
                                   NULL,
                                   editor_session_load_cb,
                                   g_slice_dup (Selection, &sel));

      if (is_active)
        active = epage;
    }

  if (active != NULL)
    _editor_page_raise (active);

  return TRUE;
}

static void
editor_session_restore_v1_drafts (EditorSession *self,
                                  GVariant      *drafts)
{
  GVariantIter iter;
  GVariant *draft;

  g_assert (EDITOR_IS_SESSION (self));
  g_assert (drafts != NULL);
  g_assert (g_variant_is_of_type (drafts, G_VARIANT_TYPE ("aa{sv}")));

  g_variant_iter_init (&iter, drafts);
  while (g_variant_iter_loop (&iter, "@a{sv}", &draft))
    {
      const gchar *draft_id;

      if (g_variant_lookup (draft, "draft-id", "&s", &draft_id))
        {
          const gchar *title;
          const gchar *uri;

          if (!g_variant_lookup (draft, "title", "&s", &title))
            title = NULL;

          if (!g_variant_lookup (draft, "uri", "&s", &uri))
            uri = NULL;

          _editor_session_add_draft (self, draft_id, title, uri);
        }
    }
}

static void
editor_session_restore_v1 (EditorSession *self,
                           GVariant      *state)
{
  g_autoptr(GVariant) drafts = NULL;
  g_autoptr(GVariant) windows = NULL;
  GVariantIter iter;
  GVariant *window;
  gboolean had_failure = FALSE;

  g_assert (EDITOR_IS_SESSION (self));
  g_assert (state != NULL);
  g_assert (g_variant_is_of_type (state, G_VARIANT_TYPE_VARDICT));

  if ((drafts = g_variant_lookup_value (state, "drafts", G_VARIANT_TYPE ("aa{sv}"))))
    editor_session_restore_v1_drafts (self, drafts);

  if (!(windows = g_variant_lookup_value (state, "windows", G_VARIANT_TYPE ("aa{sv}"))) ||
      g_variant_n_children (windows) == 0)
    goto failure;

  g_variant_iter_init (&iter, windows);
  while (g_variant_iter_loop (&iter, "@a{sv}", &window))
    {
      g_autoptr(GVariant) pages = NULL;
      guint width, height;

      if (!g_variant_lookup (window, "size", "(uu)", &width, &height) ||
          width > 10000 || height > 10000)
        {
          width = 0;
          height = 0;
        }

      if ((pages = g_variant_lookup_value (window, "pages", G_VARIANT_TYPE ("aa{sv}"))) &&
          g_variant_n_children (pages) > 0)
        {
          EditorWindow *ewin = _editor_session_create_window_no_draft (self);
          gboolean sidebar_revealed;

          if (width > 0 && height > 0)
            gtk_window_set_default_size (GTK_WINDOW (ewin), width, height);

          if (!editor_session_restore_v1_pages (self, ewin, pages))
            {
              had_failure = TRUE;
              gtk_widget_destroy (GTK_WIDGET (ewin));
              g_ptr_array_remove (self->windows, ewin);
              continue;
            }

          if (!g_variant_lookup (window, "sidebar-revealed", "b", &sidebar_revealed))
            sidebar_revealed = FALSE;

          _editor_window_set_sidebar_revealed (ewin, sidebar_revealed);

          gtk_window_present (GTK_WINDOW (ewin));
        }
    }

  if (self->windows->len == 0 || self->pages->len == 0)
    {
      if (had_failure)
        goto failure;

      /* We might hvae restored a session that had nothing persisted because
       * all the open tabs were "unmodified drafts". We don't want to warn
       * in that case, just create the window and move on.
       */
      editor_session_create_window (self);
    }

  /* XXX: this probably needs to change once we track all the recent
   * documents for quick re-use.
   */
  editor_session_restore_delete_unused (self);

  return;

failure:
  g_warning ("Failed to restore session");

  editor_session_create_window (self);
}

static void
editor_session_restore (EditorSession *self,
                        GVariant      *state)
{
  guint32 version = 0;

  g_assert (EDITOR_IS_SESSION (self));
  g_assert (state != NULL);
  g_assert (g_variant_is_of_type (state, G_VARIANT_TYPE_VARDICT));

  if (g_variant_lookup (state, "version", "u", &version) && version == 1)
    editor_session_restore_v1 (self, state);
  else
    editor_session_create_window (self);
}

static void
editor_session_restore_load_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GVariant) state = NULL;
  g_autofree gchar *contents = NULL;
  EditorSession *self;
  gsize len;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!g_file_load_contents_finish (file, result, &contents, &len, NULL, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  self = g_task_get_source_object (task);
  bytes = g_bytes_new_take (g_steal_pointer (&contents), len);
  state = g_variant_new_from_bytes (G_VARIANT_TYPE_VARDICT, bytes, FALSE);

  if (state == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_DATA,
                               "Invalid session.gvariant contents");
      return;
    }

  editor_session_restore (self, state);

  g_task_return_boolean (task, TRUE);
}

void
editor_session_restore_async (EditorSession       *self,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GFile) file = NULL;

  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  self->did_restore = TRUE;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, editor_session_restore_async);

  file = g_file_new_build_filename (g_get_user_data_dir (),
                                    APP_ID,
                                    "session.gvariant",
                                    NULL);

  g_file_load_contents_async (file,
                              cancellable,
                              editor_session_restore_load_cb,
                              g_steal_pointer (&task));
}

gboolean
editor_session_restore_finish (EditorSession  *self,
                               GAsyncResult   *result,
                               GError        **error)
{
  g_return_val_if_fail (EDITOR_IS_SESSION (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

gboolean
_editor_session_did_restore (EditorSession *self)
{
  g_return_val_if_fail (EDITOR_IS_SESSION (self), FALSE);

  return self->did_restore;
}

GPtrArray *
_editor_session_get_pages (EditorSession *self)
{
  g_return_val_if_fail (EDITOR_IS_SESSION (self), NULL);

  return self->pages;
}

static void
editor_session_load_recent_worker (GTask        *task,
                                   gpointer      source_object,
                                   gpointer      task_data,
                                   GCancellable *cancellable)
{
  GBookmarkFile *bookmarks = task_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) files = NULL;
  g_autofree gchar *filename = NULL;
  g_auto(GStrv) uris = NULL;
  gsize len;

  g_assert (G_IS_TASK (task));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_assert (bookmarks != NULL);

  filename = get_bookmarks_filename ();

  if (!g_bookmark_file_load_from_file (bookmarks, filename, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  files = g_ptr_array_new_with_free_func (g_object_unref);
  uris = g_bookmark_file_get_uris (bookmarks, &len);

  for (gsize i = 0; i < len; i++)
    g_ptr_array_add (files, g_file_new_for_uri (uris[i]));

  g_task_return_pointer (task,
                         g_steal_pointer (&files),
                         (GDestroyNotify) g_ptr_array_unref);
}

void
editor_session_load_recent_async (EditorSession       *self,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, editor_session_load_recent_async);
  g_task_set_task_data (task,
                        g_bookmark_file_new (),
                        (GDestroyNotify) g_bookmark_file_free);
  g_task_run_in_thread (task, editor_session_load_recent_worker);
}

/**
 * editor_session_load_recent_finish:
 * @self: an #EditorSession
 * @result: a #GAsyncResult provided to callback
 * @error: a location for a #GError, or %NULL
 *
 * Completes an asynchronous request to load the recently opened
 * files. This uses a private #GBookmarkFile stored in the
 * text-editor's user-data dir and is kept seperate from the
 * application state to simplify removal of the file.
 *
 * Returns: (transfer full) (element-type GFile): a #GPtrArray of #GFile or
 *   %NULL and @error is set.
 */
GPtrArray *
editor_session_load_recent_finish (EditorSession  *self,
                                   GAsyncResult   *result,
                                   GError        **error)
{
  GPtrArray *ar;

  g_return_val_if_fail (EDITOR_IS_SESSION (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  if ((ar = g_task_propagate_pointer (G_TASK (result), error)))
    g_ptr_array_set_free_func (ar, NULL);

  return g_steal_pointer (&ar);
}

void
_editor_session_document_seen (EditorSession  *self,
                               EditorDocument *document)
{
  GFile *file;

  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (EDITOR_IS_DOCUMENT (document));

  if ((file = editor_document_get_file (document)))
    {
      if (!g_hash_table_contains (self->seen, file))
        g_hash_table_insert (self->seen, g_file_dup (file), NULL);
    }
}

GArray *
_editor_session_get_drafts (EditorSession *self)
{
  g_return_val_if_fail (EDITOR_IS_SESSION (self), NULL);

  return self->drafts;
}

void
_editor_session_add_draft (EditorSession *self,
                           const gchar   *draft_id,
                           const gchar   *title,
                           const gchar   *uri)
{
  EditorSessionDraft d;

  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (draft_id != NULL);

  for (guint i = 0; i < self->drafts->len; i++)
    {
      EditorSessionDraft *draft = &g_array_index (self->drafts, EditorSessionDraft, i);

      if (g_strcmp0 (draft->draft_id, draft_id) == 0)
        {
          if (g_strcmp0 (draft->title, title) != 0)
            {
              g_clear_pointer (&draft->title, g_free);
              draft->title = g_strdup (title);
            }

          if (g_strcmp0 (draft->uri, uri) != 0)
            {
              g_clear_pointer (&draft->uri, g_free);
              draft->uri = g_strdup (uri);
            }

          return;
        }
    }

  d.draft_id = g_strdup (draft_id);
  d.title = g_strdup (title);
  d.uri = g_strdup (uri);

  g_array_append_val (self->drafts, d);
}

void
_editor_session_remove_draft (EditorSession *self,
                              const gchar   *draft_id)
{
  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (draft_id != NULL);

  for (guint i = 0; i < self->drafts->len; i++)
    {
      const EditorSessionDraft *draft = &g_array_index (self->drafts, EditorSessionDraft, i);

      if (g_strcmp0 (draft->draft_id, draft_id) == 0)
        {
          g_array_remove_index (self->drafts, i);
          return;
        }
    }
}

void
_editor_session_move_page_to_window (EditorSession *self,
                                     EditorPage    *page,
                                     EditorWindow  *window)
{
  EditorWindow *old_window;

  g_return_if_fail (EDITOR_IS_SESSION (self));
  g_return_if_fail (EDITOR_IS_PAGE (page));
  g_return_if_fail (EDITOR_IS_WINDOW (window));

  old_window = _editor_page_get_window (page);

  if (window != old_window)
    {
      g_object_ref (page);
      _editor_window_remove_page (old_window, page);
      _editor_window_add_page (window, page);
      g_object_unref (page);
    }
}

static gboolean
editor_session_auto_save_timeout_cb (gpointer user_data)
{
  EditorSession *self = user_data;

  g_assert (EDITOR_IS_SESSION (self));

  g_debug ("Performing auto-save of session state\n");
  editor_session_save_async (self, NULL, NULL, NULL);

  return G_SOURCE_CONTINUE;
}

/**
 * editor_session_get_auto_save:
 * @self: an #EditorSession
 *
 * Gets the #EditorSession:auto-save property.
 *
 * Returns: %TRUE if auto-save is enabled.
 */
gboolean
editor_session_get_auto_save (EditorSession *self)
{
  g_return_val_if_fail (EDITOR_IS_SESSION (self), FALSE);

  return self->auto_save;
}

/**
 * editor_session_set_auto_save:
 * @self: an #EditorSesion
 * @auto_save: if session auto-save should be enabled
 *
 * Sets the #EditorSession:auto-save property.
 *
 * If set to %TRUE, then the session will auto-save it's state
 * so that the session may be restored in case of a crash.
 *
 * This does not save documents to their backing files, but instead
 * auto saves the drafts for the file.
 */
void
editor_session_set_auto_save (EditorSession *self,
                              gboolean       auto_save)
{
  g_return_if_fail (EDITOR_IS_SESSION (self));

  auto_save = !!auto_save;

  if (auto_save != self->auto_save)
    {
      self->auto_save = auto_save;
      g_clear_handle_id (&self->auto_save_source, g_source_remove);
      if (auto_save)
        self->auto_save_source =
          g_timeout_add_seconds (AUTO_SAVE_TIMEOUT_SECONDS,
                                 editor_session_auto_save_timeout_cb,
                                 self);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_AUTO_SAVE]);
    }
}
