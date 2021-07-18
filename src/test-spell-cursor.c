#include "editor-spell-cursor.c"
#include "cjhtextregion.c"

static const char *test_text = "this is a series of words";

static char *
next_word (EditorSpellCursor *cursor)
{
  GtkTextIter begin, end;

  if (editor_spell_cursor_next (cursor, &begin, &end))
    return gtk_text_iter_get_slice (&begin, &end);

  return NULL;
}

static void
test_cursor (void)
{
  g_autoptr(GtkTextBuffer) buffer = gtk_text_buffer_new (NULL);
  CjhTextRegion *region = _cjh_text_region_new (NULL, NULL);
  g_autoptr(EditorSpellCursor) cursor = editor_spell_cursor_new (buffer, region, NULL, NULL);
  char *word;

  gtk_text_buffer_set_text (buffer, test_text, -1);
  _cjh_text_region_insert (region, 0, strlen (test_text), NULL);

  word = next_word (cursor);
  g_assert_cmpstr (word, ==, "this");

  word = next_word (cursor);
  g_assert_cmpstr (word, ==, "is");

  word = next_word (cursor);
  g_assert_cmpstr (word, ==, "a");

  word = next_word (cursor);
  g_assert_cmpstr (word, ==, "series");

  word = next_word (cursor);
  g_assert_cmpstr (word, ==, "of");

  word = next_word (cursor);
  g_assert_cmpstr (word, ==, "words");

  word = next_word (cursor);
  g_assert_cmpstr (word, ==, NULL);

  _cjh_text_region_free (region);
}

static void
test_cursor_in_word (void)
{
  g_autoptr(GtkTextBuffer) buffer = gtk_text_buffer_new (NULL);
  CjhTextRegion *region = _cjh_text_region_new (NULL, NULL);
  g_autoptr(EditorSpellCursor) cursor = editor_spell_cursor_new (buffer, region, NULL, NULL);
  const char *pos = strstr (test_text, "ries "); /* se|ries */
  gsize offset = pos - test_text;
  char *word;

  gtk_text_buffer_set_text (buffer, test_text, -1);
  _cjh_text_region_insert (region, 0, strlen (test_text), GINT_TO_POINTER (1));
  _cjh_text_region_replace (region, offset, strlen (test_text) - offset, NULL);

  word = next_word (cursor);
  g_assert_cmpstr (word, ==, "series");

  word = next_word (cursor);
  g_assert_cmpstr (word, ==, "of");

  word = next_word (cursor);
  g_assert_cmpstr (word, ==, "words");

  word = next_word (cursor);
  g_assert_cmpstr (word, ==, NULL);

  _cjh_text_region_free (region);
}

int
main (int argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Spelling/Cursor/basic", test_cursor);
  g_test_add_func ("/Spelling/Cursor/in_word", test_cursor_in_word);
  return g_test_run ();
}
