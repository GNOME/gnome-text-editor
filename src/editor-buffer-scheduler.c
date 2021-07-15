/* editor-buffer-scheduler.c
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

#include "editor-buffer-scheduler.h"

/* The goal of this GSource is to let us pile on a bunch of background work but
 * only do a small amount of it at a time per-frame cycle. This becomes more
 * important when you have multiple documents open all competing to do
 * background work and potentially stalling the main loop.
 *
 * Instead, we only do 1 msec of work at a time and then wait for the next
 * frame to come in.
 *
 * This is somewhat hacky because we don't have the widgets to work off of,
 * only the buffers which have no frame clocks. So we guess the amount of
 * time between frames based on the longest monitor delay (lowest FPS).
 */

#define QUANTA_USEC (G_USEC_PER_SEC / 1000L) /* 1 msec */

typedef struct
{
  GSource source;
  GQueue queue;
  gint64 interval;
  gsize last_handler_id;
} EditorBufferScheduler;

typedef struct
{
  GList link;
  EditorBufferCallback callback;
  gpointer user_data;
  GDestroyNotify notify;
  gint64 ready_time;
  gsize id;
} EditorBufferTask;

static GSource *the_source;

static void
editor_buffer_task_free (EditorBufferTask *task)
{
  g_assert (task != NULL);
  g_assert (task->link.data == (gpointer)task);
  g_assert (task->link.next == NULL);
  g_assert (task->link.prev == NULL);
  g_assert (task->callback != NULL);

  if (task->notify != NULL)
    task->notify (task->user_data);

  g_slice_free (EditorBufferTask, task);
}

static EditorBufferTask *
editor_buffer_task_new (EditorBufferCallback  callback,
                        gpointer              user_data,
                        GDestroyNotify        notify)
{
  EditorBufferTask *task;

  g_return_val_if_fail (callback != NULL, NULL);

  task = g_slice_new0 (EditorBufferTask);
  task->link.data = task;
  task->callback = callback;
  task->user_data = user_data;
  task->notify = notify;
  task->ready_time = 0; /* Now */
  task->id = 0;

  return task;
}

static gint64
get_interval (EditorBufferScheduler *self)
{
  if G_UNLIKELY (self->interval == 0)
    {
      GdkDisplay *display = gdk_display_get_default ();
      GListModel *monitors = gdk_display_get_monitors (display);
      guint n_items = g_list_model_get_n_items (monitors);
      gint64 lowest_interval = 60000;

      for (guint i = 0; i < n_items; i++)
      {
        g_autoptr(GdkMonitor) monitor = g_list_model_get_item (monitors, i);
        gint64 interval = gdk_monitor_get_refresh_rate (monitor);

        if (interval != 0 && interval < lowest_interval)
          lowest_interval = interval;
      }

      self->interval = (double)G_USEC_PER_SEC / (double)lowest_interval * 1000.0;
    }

  return self->interval;
}

static gboolean
editor_buffer_scheduler_prepare (GSource *source,
                                 int     *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
editor_buffer_scheduler_check (GSource *source)
{
  EditorBufferScheduler *self = (EditorBufferScheduler *)source;
  EditorBufferTask *task = g_queue_peek_head (&self->queue);

  return task != NULL && task->ready_time <= g_source_get_time (source);
}

static gboolean
editor_buffer_scheduler_dispatch (GSource     *source,
                                  GSourceFunc  source_func,
                                  gpointer     user_data)
{
  EditorBufferScheduler *self = (EditorBufferScheduler *)source;
  gint64 current = g_source_get_time (source);
  gint64 deadline = current + QUANTA_USEC;
  gint64 interval = get_interval (self);

  /* Try to process as many items within our quanta if they */
  while (g_get_monotonic_time () < deadline)
    {
      EditorBufferTask *task = g_queue_peek_head (&self->queue);

      if (task == NULL)
        break;

      g_queue_unlink (&self->queue, &task->link);

      if (task->callback (deadline, task->user_data))
        {
          task->ready_time = current + interval;
          g_queue_push_tail_link (&self->queue, &task->link);
          break;
        }

      editor_buffer_task_free (task);
    }

  if (self->queue.head != NULL)
    {
      EditorBufferTask *task = g_queue_peek_head (&self->queue);
      g_source_set_ready_time (source, task->ready_time);
      return G_SOURCE_CONTINUE;
    }

  return G_SOURCE_REMOVE;
}

static void
editor_buffer_scheduler_finalize (GSource *source)
{
  EditorBufferScheduler *self = (EditorBufferScheduler *)source;

  g_assert (self->queue.length == 0);
  g_assert (self->queue.head == NULL);
  g_assert (self->queue.tail == NULL);

  if (source == the_source)
    the_source = NULL;
}

static GSourceFuncs source_funcs = {
  editor_buffer_scheduler_prepare,
  editor_buffer_scheduler_check,
  editor_buffer_scheduler_dispatch,
  editor_buffer_scheduler_finalize,
};

static EditorBufferScheduler *
get_scheduler (void)
{
  if (the_source == NULL)
    {
      the_source = g_source_new (&source_funcs, sizeof (EditorBufferScheduler));
      g_source_set_name (the_source, "EditorBufferScheduler");
      g_source_set_priority (the_source, G_PRIORITY_LOW);
      g_source_set_ready_time (the_source, 0);
      g_source_attach (the_source, g_main_context_default ());
      g_source_unref (the_source);
    }

  return (EditorBufferScheduler *)the_source;
}

gsize
editor_buffer_scheduler_add (EditorBufferCallback callback,
                             gpointer             user_data)
{
  return editor_buffer_scheduler_add_full (callback, user_data, NULL);
}

gsize
editor_buffer_scheduler_add_full (EditorBufferCallback callback,
                                  gpointer             user_data,
                                  GDestroyNotify       notify)
{
  EditorBufferScheduler *self;
  EditorBufferTask *task;

  g_return_val_if_fail (callback != NULL, 0);

  self = get_scheduler ();
  task = editor_buffer_task_new (callback, user_data, notify);
  task->id = ++self->last_handler_id;

  /* Request progress immediately */
  g_queue_push_head_link (&self->queue, &task->link);
  g_source_set_ready_time ((GSource *)self, g_source_get_time ((GSource *)self));

  return task->id;
}

void
editor_buffer_scheduler_remove (gsize handler_id)
{
  EditorBufferScheduler *self;

  g_return_if_fail (handler_id != 0);

  self = get_scheduler ();

  for (const GList *iter = self->queue.head; iter != NULL; iter = iter->next)
    {
      EditorBufferTask *task = iter->data;

      if (task->id == handler_id)
        {
          g_queue_unlink (&self->queue, &task->link);
          editor_buffer_task_free (task);
          break;
        }
    }

  if (self->queue.head != NULL)
    {
      EditorBufferTask *task = g_queue_peek_head (&self->queue);
      g_source_set_ready_time ((GSource *)self, task->ready_time);
    }
  else
    {
      g_source_destroy ((GSource *)self);
    }
}
