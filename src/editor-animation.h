/* editor-animation.h
 *
 * Copyright (C) 2010-2020 Christian Hergert <christian@hergert.me>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <gdk/gdk.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_ANIMATION      (editor_animation_get_type())
#define EDITOR_TYPE_ANIMATION_MODE (editor_animation_mode_get_type())

G_DECLARE_FINAL_TYPE (EditorAnimation, editor_animation, EDITOR, ANIMATION, GInitiallyUnowned)

typedef enum _EditorAnimationMode
{
  EDITOR_ANIMATION_LINEAR,
  EDITOR_ANIMATION_EASE_IN_QUAD,
  EDITOR_ANIMATION_EASE_OUT_QUAD,
  EDITOR_ANIMATION_EASE_IN_OUT_QUAD,
  EDITOR_ANIMATION_EASE_IN_CUBIC,
  EDITOR_ANIMATION_EASE_OUT_CUBIC,
  EDITOR_ANIMATION_EASE_IN_OUT_CUBIC,
  EDITOR_ANIMATION_LAST
} EditorAnimationMode;

GType            editor_animation_mode_get_type      (void) G_GNUC_CONST;
void             editor_animation_start              (EditorAnimation     *animation);
void             editor_animation_stop               (EditorAnimation     *animation);
void             editor_animation_add_property       (EditorAnimation     *animation,
                                                      GParamSpec          *pspec,
                                                      const GValue        *value);
EditorAnimation *editor_object_animatev              (gpointer             object,
                                                      EditorAnimationMode  mode,
                                                      guint                duration_msec,
                                                      GdkFrameClock       *frame_clock,
                                                      const gchar         *first_property,
                                                      va_list              args);
EditorAnimation *editor_object_animate               (gpointer             object,
                                                      EditorAnimationMode  mode,
                                                      guint                duration_msec,
                                                      GdkFrameClock       *frame_clock,
                                                      const gchar         *first_property,
                                                      ...) G_GNUC_NULL_TERMINATED;
EditorAnimation *editor_object_animate_full          (gpointer             object,
                                                      EditorAnimationMode  mode,
                                                      guint                duration_msec,
                                                      GdkFrameClock       *frame_clock,
                                                      GDestroyNotify       notify,
                                                      gpointer             notify_data,
                                                      const gchar         *first_property,
                                                      ...) G_GNUC_NULL_TERMINATED;
guint            editor_animation_calculate_duration (GdkMonitor          *monitor,
                                                      gdouble              from_value,
                                                      gdouble              to_value);

G_END_DECLS
