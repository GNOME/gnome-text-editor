/*
 * editor-draft.c
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

#include "config.h"

#include "editor-draft.h"

struct _EditorDraft
{
  GObject  parent_instance;
  char    *uuid;
};

enum {
  PROP_0,
  PROP_FILE,
  PROP_UUID,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorDraft, editor_draft, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static void
editor_draft_finalize (GObject *object)
{
  EditorDraft *self = (EditorDraft *)object;

  g_clear_pointer (&self->uuid, g_free);

  G_OBJECT_CLASS (editor_draft_parent_class)->finalize (object);
}

static void
editor_draft_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  EditorDraft *self = EDITOR_DRAFT (object);

  switch (prop_id)
    {
    case PROP_FILE:
      g_value_take_object (value, editor_draft_dup_file (self));
      break;

    case PROP_UUID:
      g_value_take_string (value, editor_draft_dup_uuid (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_draft_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  EditorDraft *self = EDITOR_DRAFT (object);

  switch (prop_id)
    {
    case PROP_UUID:
      self->uuid = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_draft_class_init (EditorDraftClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = editor_draft_finalize;
  object_class->get_property = editor_draft_get_property;
  object_class->set_property = editor_draft_set_property;

  properties[PROP_FILE] =
    g_param_spec_object ("file", NULL, NULL,
                         G_TYPE_FILE,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_UUID] =
    g_param_spec_string ("uuid", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_draft_init (EditorDraft *self)
{
}

char *
editor_draft_dup_uuid (EditorDraft *self)
{
  g_return_val_if_fail (EDITOR_IS_DRAFT (self), NULL);
  g_return_val_if_fail (self->uuid != NULL, NULL);

  return g_strdup (self->uuid);
}

GFile *
editor_draft_dup_file (EditorDraft *self)
{
  g_return_val_if_fail (EDITOR_IS_DRAFT (self), NULL);
  g_return_val_if_fail (self->uuid != NULL, NULL);

  return g_file_new_build_filename (g_get_user_data_dir (), APP_ID, "drafts", self->uuid, NULL);
}

DexFuture *
editor_draft_delete (EditorDraft *self)
{
  g_autoptr(GFile) file = NULL;

  dex_return_error_if_fail (EDITOR_IS_DRAFT (self));
  dex_return_error_if_fail (self->uuid != NULL);

  return dex_file_delete ((file = editor_draft_dup_file (self)), G_PRIORITY_DEFAULT);
}
