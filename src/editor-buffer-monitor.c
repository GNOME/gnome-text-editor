/* editor-buffer-monitor.c
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

#define G_LOG_DOMAIN "editor-buffer-monitor"

#include "config.h"

#include "editor-buffer-monitor-private.h"

struct _EditorBufferMonitor
{
  GObject parent_instance;
  GFile *file;
  GFileMonitor *monitor;
  int pause_count;
  guint changed_source;
  guint changed : 1;
};

G_DEFINE_TYPE (EditorBufferMonitor, editor_buffer_monitor, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_CHANGED,
  PROP_FILE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * editor_buffer_monitor_new:
 *
 * Create a new #EditorBufferMonitor.
 *
 * Returns: (transfer full): a newly created #EditorBufferMonitor
 */
EditorBufferMonitor *
editor_buffer_monitor_new (void)
{
  return g_object_new (EDITOR_TYPE_BUFFER_MONITOR, NULL);
}

static void
editor_buffer_monitor_dispose (GObject *object)
{
  EditorBufferMonitor *self = (EditorBufferMonitor *)object;

  g_clear_handle_id (&self->changed_source, g_source_remove);
  if (self->monitor != NULL)
    g_file_monitor_cancel (self->monitor);
  g_clear_object (&self->monitor);
  g_clear_object (&self->file);

  G_OBJECT_CLASS (editor_buffer_monitor_parent_class)->dispose (object);
}

static void
editor_buffer_monitor_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  EditorBufferMonitor *self = EDITOR_BUFFER_MONITOR (object);

  switch (prop_id)
    {
    case PROP_FILE:
      g_value_set_object (value, editor_buffer_monitor_get_file (self));
      break;

    case PROP_CHANGED:
      g_value_set_boolean (value, editor_buffer_monitor_get_changed (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_buffer_monitor_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  EditorBufferMonitor *self = EDITOR_BUFFER_MONITOR (object);

  switch (prop_id)
    {
    case PROP_FILE:
      editor_buffer_monitor_set_file (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_buffer_monitor_class_init (EditorBufferMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_buffer_monitor_dispose;
  object_class->get_property = editor_buffer_monitor_get_property;
  object_class->set_property = editor_buffer_monitor_set_property;

  properties [PROP_FILE] =
    g_param_spec_object ("file",
                         "File",
                         "The file to be monitored",
                         G_TYPE_FILE,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CHANGED] =
    g_param_spec_boolean ("changed",
                          "Changed",
                          "If the buffer was externally modified",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_buffer_monitor_init (EditorBufferMonitor *self)
{
}

static gboolean
notify_timeout_cb (gpointer user_data)
{
  EditorBufferMonitor *self = user_data;

  g_assert (EDITOR_IS_BUFFER_MONITOR (self));

  self->changed_source = 0;

  if (!self->changed)
    {
      self->changed = TRUE;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CHANGED]);
    }

  return G_SOURCE_REMOVE;
}

static void
editor_buffer_monitor_changed_cb (EditorBufferMonitor *self,
                                  GFile               *file,
                                  GFile               *other_file,
                                  GFileMonitorEvent    event,
                                  GFileMonitor        *monitor)
{
  g_assert (EDITOR_IS_BUFFER_MONITOR (self));
  g_assert (G_IS_FILE (file));
  g_assert (!other_file || G_IS_FILE (other_file));
  g_assert (G_IS_FILE_MONITOR (monitor));

  if (monitor != self->monitor)
    return;

  if (self->pause_count > 0)
    return;

  if (event == G_FILE_MONITOR_EVENT_CHANGED ||
      event == G_FILE_MONITOR_EVENT_DELETED ||
      event == G_FILE_MONITOR_EVENT_RENAMED)
    {
      if (self->changed_source == 0)
        self->changed_source = g_timeout_add_full (G_PRIORITY_DEFAULT,
                                                   100,
                                                   notify_timeout_cb,
                                                   g_object_ref (self),
                                                   g_object_unref);
    }
}

GFile *
editor_buffer_monitor_get_file (EditorBufferMonitor *self)
{
  g_return_val_if_fail (EDITOR_IS_BUFFER_MONITOR (self), NULL);

  return self->file;
}

void
editor_buffer_monitor_set_file (EditorBufferMonitor *self,
                                GFile               *file)
{
  g_return_if_fail (EDITOR_IS_BUFFER_MONITOR (self));

  if (g_set_object (&self->file, file))
    {
      editor_buffer_monitor_reset (self);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_FILE]);
    }
}

gboolean
editor_buffer_monitor_get_changed (EditorBufferMonitor *self)
{
  g_return_val_if_fail (EDITOR_IS_BUFFER_MONITOR (self), FALSE);

  return self->changed;
}

void
editor_buffer_monitor_reset (EditorBufferMonitor *self)
{
  g_return_if_fail (EDITOR_IS_BUFFER_MONITOR (self));

  if (self->changed)
    {
      self->changed = FALSE;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CHANGED]);
    }

  g_clear_handle_id (&self->changed_source, g_source_remove);

  if (self->monitor != NULL)
    g_file_monitor_cancel (self->monitor);
  g_clear_object (&self->monitor);

  if (self->file != NULL && self->pause_count == 0)
    {
      self->monitor = g_file_monitor_file (self->file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);

      if (self->monitor != NULL)
        {
          g_file_monitor_set_rate_limit (self->monitor, 500);
          g_signal_connect_object (self->monitor,
                                   "changed",
                                   G_CALLBACK (editor_buffer_monitor_changed_cb),
                                   self,
                                   G_CONNECT_SWAPPED);
        }
    }
}

void
editor_buffer_monitor_pause (EditorBufferMonitor *self)
{
  g_return_if_fail (EDITOR_IS_BUFFER_MONITOR (self));

  self->pause_count++;

  if (self->changed)
    {
      self->changed = FALSE;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CHANGED]);
    }

  g_clear_handle_id (&self->changed_source, g_source_remove);

  if (self->monitor != NULL)
    g_file_monitor_cancel (self->monitor);
  g_clear_object (&self->monitor);
}

void
editor_buffer_monitor_unpause (EditorBufferMonitor *self)
{
  g_return_if_fail (EDITOR_IS_BUFFER_MONITOR (self));
  g_return_if_fail (self->pause_count > 0);
  g_return_if_fail (self->monitor == NULL);

  self->pause_count--;

  if (self->pause_count == 0)
    editor_buffer_monitor_reset (self);
}
