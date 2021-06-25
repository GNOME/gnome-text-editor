/* editor-enchant-spell-provider.c
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

#include <glib/gi18n.h>
#include <enchant.h>

#include "editor-enchant-spell-language.h"
#include "editor-enchant-spell-provider.h"

struct _EditorEnchantSpellProvider
{
  EditorSpellProvider parent_instance;
};

G_DEFINE_TYPE (EditorEnchantSpellProvider, editor_enchant_spell_provider, EDITOR_TYPE_SPELL_PROVIDER)

static GHashTable *languages;

static EnchantBroker *
get_broker (void)
{
  static EnchantBroker *broker;

  if (broker == NULL)
    broker = enchant_broker_init ();

  return broker;
}

/**
 * editor_enchant_spell_provider_new:
 *
 * Create a new #EditorEnchantSpellProvider.
 *
 * Returns: (transfer full): a newly created #EditorEnchantSpellProvider
 */
EditorSpellProvider *
editor_enchant_spell_provider_new (void)
{
  return g_object_new (EDITOR_TYPE_ENCHANT_SPELL_PROVIDER,
                       "display-name", _("Enchant 2"),
                       NULL);
}

static gboolean
editor_enchant_spell_provider_supports_language (EditorSpellProvider *provider,
                                                 const char          *language)
{
  EditorEnchantSpellProvider *self = (EditorEnchantSpellProvider *)provider;

  g_assert (EDITOR_IS_ENCHANT_SPELL_PROVIDER (self));
  g_assert (language != NULL);

  return enchant_broker_dict_exists (get_broker (), language);
}

static void
list_languages_cb (const char * const  lang_tag,
                   const char * const  provider_name,
                   const char * const  provider_desc,
                   const char * const  provider_file,
                   void               *user_data)
{
  GArray *ar = user_data;
  char *code = g_strdup (lang_tag);
  g_array_append_val (ar, code);
}

static char **
editor_enchant_spell_provider_list_languages (EditorSpellProvider *provider)
{
  EnchantBroker *broker = get_broker ();
  GArray *ar = g_array_new (TRUE, FALSE, sizeof (char *));
  enchant_broker_list_dicts (broker, list_languages_cb, ar);
  return (char **)(gpointer)g_array_free (ar, FALSE);
}

static EditorSpellLanguage *
editor_enchant_spell_provider_get_language (EditorSpellProvider *provider,
                                            const char          *language)
{
  EditorSpellLanguage *ret;

  g_assert (EDITOR_IS_ENCHANT_SPELL_PROVIDER (provider));
  g_assert (language != NULL);

  if (languages == NULL)
    languages = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);

  if (!(ret = g_hash_table_lookup (languages, language)))
    {
      EnchantDict *dict = enchant_broker_request_dict (get_broker (), language);

      if (dict == NULL)
        return NULL;

      ret = editor_enchant_spell_language_new (language, dict);
      g_hash_table_insert (languages, (char *)g_intern_string (language), ret);
    }

  return ret ? g_object_ref (ret) : NULL;
}

static void
editor_enchant_spell_provider_class_init (EditorEnchantSpellProviderClass *klass)
{
  EditorSpellProviderClass *spell_provider_class = EDITOR_SPELL_PROVIDER_CLASS (klass);

  spell_provider_class->supports_language = editor_enchant_spell_provider_supports_language;
  spell_provider_class->list_languages = editor_enchant_spell_provider_list_languages;
  spell_provider_class->get_language = editor_enchant_spell_provider_get_language;
}

static void
editor_enchant_spell_provider_init (EditorEnchantSpellProvider *self)
{
}
