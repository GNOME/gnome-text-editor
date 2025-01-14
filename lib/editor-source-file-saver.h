/*
 * editor-source-file-saver.h
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#pragma once

#include <gtksourceview/gtksource.h>
#include <libdex.h>

#include "editor-progress.h"

G_BEGIN_DECLS

static void
editor_source_file_saver_save_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  g_autoptr(DexPromise) promise = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (GTK_SOURCE_IS_FILE_SAVER (object));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (promise));

  if (!gtk_source_file_saver_save_finish (GTK_SOURCE_FILE_SAVER (object), result, &error))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_boolean (promise, TRUE);
}

static inline DexFuture *
editor_source_file_saver_save (GtkSourceFileSaver *saver,
                               int                 io_priority,
                               EditorProgress     *progress)
{
  DexPromise *promise;

  dex_return_error_if_fail (GTK_SOURCE_IS_FILE_SAVER (saver));
  dex_return_error_if_fail (!progress || EDITOR_IS_PROGRESS (progress));

  promise = dex_promise_new_cancellable ();

  gtk_source_file_saver_save_async (saver,
                                     io_priority,
                                     dex_promise_get_cancellable (promise),
                                     editor_progress_file_callback,
                                     progress ? g_object_ref (progress) : editor_progress_new (),
                                     g_object_unref,
                                     editor_source_file_saver_save_cb,
                                     dex_ref (promise));

  return DEX_FUTURE (promise);
}

G_END_DECLS
