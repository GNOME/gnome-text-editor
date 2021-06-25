/* editor-enchant-spell-language.c
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

#include <enchant.h>

#include "editor-enchant-spell-language.h"

struct _EditorEnchantSpellLanguage
{
  EditorSpellLanguage parent_instance;
  EnchantDict *native;
};

G_DEFINE_TYPE (EditorEnchantSpellLanguage, editor_enchant_spell_language, EDITOR_TYPE_SPELL_LANGUAGE)

enum {
  PROP_0,
  PROP_NATIVE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * editor_enchant_spell_language_new:
 *
 * Create a new #EditorEnchantSpellLanguage.
 *
 * Returns: (transfer full): a newly created #EditorEnchantSpellLanguage
 */
EditorSpellLanguage *
editor_enchant_spell_language_new (const char *code,
                                   gpointer    native)
{
  return g_object_new (EDITOR_TYPE_ENCHANT_SPELL_LANGUAGE,
                       "code", code,
                       "native", native,
                       NULL);
}

static gboolean
editor_enchant_spell_language_contains_word (EditorSpellLanguage *language,
                                             const char          *word,
                                             gssize               word_len)
{
  EditorEnchantSpellLanguage *self = (EditorEnchantSpellLanguage *)language;

  g_assert (EDITOR_IS_ENCHANT_SPELL_LANGUAGE (self));
  g_assert (word != NULL);
  g_assert (word_len > 0);

  return enchant_dict_check (self->native, word, word_len) == 0;
}

static char **
editor_enchant_spell_language_list_corrections (EditorSpellLanguage *language,
                                                const char          *word,
                                                gssize               word_len)
{
  EditorEnchantSpellLanguage *self = (EditorEnchantSpellLanguage *)language;
  size_t count = 0;
  char **tmp;
  char **ret = NULL;

  g_assert (EDITOR_IS_ENCHANT_SPELL_LANGUAGE (self));
  g_assert (word != NULL);
  g_assert (word_len > 0);

  if ((tmp = enchant_dict_suggest (self->native, word, word_len, &count)) && count > 0)
    {
      ret = g_strdupv (tmp);
      enchant_dict_free_string_list (self->native, tmp);
    }

  return g_steal_pointer (&ret);
}

static void
editor_enchant_spell_language_finalize (GObject *object)
{
  EditorEnchantSpellLanguage *self = (EditorEnchantSpellLanguage *)object;

  /* Owned by provider */
  self->native = NULL;

  G_OBJECT_CLASS (editor_enchant_spell_language_parent_class)->finalize (object);
}

static void
editor_enchant_spell_language_get_property (GObject    *object,
                                            guint       prop_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
  EditorEnchantSpellLanguage *self = EDITOR_ENCHANT_SPELL_LANGUAGE (object);

  switch (prop_id)
    {
    case PROP_NATIVE:
      g_value_set_pointer (value, self->native);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_enchant_spell_language_set_property (GObject      *object,
                                            guint         prop_id,
                                            const GValue *value,
                                            GParamSpec   *pspec)
{
  EditorEnchantSpellLanguage *self = EDITOR_ENCHANT_SPELL_LANGUAGE (object);

  switch (prop_id)
    {
    case PROP_NATIVE:
      self->native = g_value_get_pointer (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_enchant_spell_language_class_init (EditorEnchantSpellLanguageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  EditorSpellLanguageClass *spell_language_class = EDITOR_SPELL_LANGUAGE_CLASS (klass);

  object_class->finalize = editor_enchant_spell_language_finalize;
  object_class->get_property = editor_enchant_spell_language_get_property;
  object_class->set_property = editor_enchant_spell_language_set_property;

  spell_language_class->contains_word = editor_enchant_spell_language_contains_word;
  spell_language_class->list_corrections = editor_enchant_spell_language_list_corrections;

  properties [PROP_NATIVE] =
    g_param_spec_pointer ("native",
                          "Native",
                          "The native enchant dictionary",
                          (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_enchant_spell_language_init (EditorEnchantSpellLanguage *self)
{
}

gpointer
editor_enchant_spell_language_get_native (EditorEnchantSpellLanguage *self)
{
  g_return_val_if_fail (EDITOR_IS_ENCHANT_SPELL_LANGUAGE (self), NULL);

  return self->native;
}
