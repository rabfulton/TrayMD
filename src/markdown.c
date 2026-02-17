#include "markdown.h"
#include "config.h"
#include <ctype.h>
#include <string.h>

/* Tag names */
#define TAG_H1 "h1"
#define TAG_H2 "h2"
#define TAG_H3 "h3"
#define TAG_BOLD "bold"
#define TAG_ITALIC "italic"
#define TAG_CODE "code"
#define TAG_CODE_BLOCK "code_block"
#define TAG_QUOTE "quote"
#define TAG_LIST "list"
#define TAG_LIST_BULLET "list_bullet"
#define TAG_LINK "link"
#define TAG_HRULE "hrule"
#define TAG_INVISIBLE "invisible"

static void collect_anchor_offsets(GtkTextBuffer *buffer, const gchar *data_key,
                                   GArray *offsets) {
  GtkTextIter iter, end;

  gtk_text_buffer_get_bounds(buffer, &iter, &end);
  while (!gtk_text_iter_equal(&iter, &end)) {
    GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&iter);
    if (anchor && g_object_get_data(G_OBJECT(anchor), data_key) != NULL) {
      gint offset = gtk_text_iter_get_offset(&iter);
      g_array_append_val(offsets, offset);
    }
    gtk_text_iter_forward_char(&iter);
  }
}

static gint compare_int_desc(gconstpointer a, gconstpointer b) {
  const gint ia = *(const gint *)a;
  const gint ib = *(const gint *)b;
  return (ib - ia);
}

static void delete_char_offsets(GtkTextBuffer *buffer, GArray *offsets) {
  if (!offsets || offsets->len == 0) {
    return;
  }

  g_array_sort(offsets, compare_int_desc);
  for (guint i = 0; i < offsets->len; i++) {
    gint offset = g_array_index(offsets, gint, i);
    GtkTextIter start, end;

    gtk_text_buffer_get_iter_at_offset(buffer, &start, offset);
    end = start;
    if (gtk_text_iter_forward_char(&end)) {
      gtk_text_buffer_delete(buffer, &start, &end);
    }
  }
}

void markdown_init_tags(GtkTextBuffer *buffer) {
  /* Invisible tag - hides markdown syntax characters */
  gtk_text_buffer_create_tag(buffer, TAG_INVISIBLE, "invisible", TRUE, NULL);

  /* Header 1 - Large bold */
  gtk_text_buffer_create_tag(buffer, TAG_H1, "weight", PANGO_WEIGHT_BOLD,
                             "scale", 2.0, "foreground", config->h1_color,
                             "pixels-below-lines", 12, NULL);

  /* Header 2 - Medium bold */
  gtk_text_buffer_create_tag(buffer, TAG_H2, "weight", PANGO_WEIGHT_BOLD,
                             "scale", 1.6, "foreground", config->h2_color,
                             "pixels-below-lines", 10, NULL);

  /* Header 3 - Small bold */
  gtk_text_buffer_create_tag(buffer, TAG_H3, "weight", PANGO_WEIGHT_BOLD,
                             "scale", 1.3, "foreground", config->h3_color,
                             "pixels-below-lines", 8, NULL);

  /* Bold */
  gtk_text_buffer_create_tag(buffer, TAG_BOLD, "weight", PANGO_WEIGHT_BOLD,
                             NULL);

  /* Italic */
  gtk_text_buffer_create_tag(buffer, TAG_ITALIC, "style", PANGO_STYLE_ITALIC,
                             NULL);

  /* Code content - monospace with background */
  gtk_text_buffer_create_tag(buffer, TAG_CODE, "family", "Monospace",
                             "background", "#3E4451", "foreground", "#E06C75",
                             NULL);

  /* Fenced code block */
  gtk_text_buffer_create_tag(
      buffer, TAG_CODE_BLOCK, "family", "Monospace", "foreground", "#ABB2BF",
      "paragraph-background", "#2C313A", "left-margin", 24, "right-margin", 16,
      NULL);

  /* Quote - Indented and styled */
  gtk_text_buffer_create_tag(buffer, TAG_QUOTE, "left-margin", 24, "style",
                             PANGO_STYLE_ITALIC, "foreground", "#5C6370",
                             "paragraph-background", "#2C313A", NULL);

  /* List item - indentation */
  gtk_text_buffer_create_tag(buffer, TAG_LIST, "left-margin", 28, NULL);

  /* List bullet styling */
  gtk_text_buffer_create_tag(buffer, TAG_LIST_BULLET, "foreground",
                             config->list_bullet_color, NULL);

  /* Link - Blue underlined */
  gtk_text_buffer_create_tag(buffer, TAG_LINK, "foreground", "#61AFEF",
                             "underline", PANGO_UNDERLINE_SINGLE, NULL);

  /* Horizontal rule */
  gtk_text_buffer_create_tag(buffer, TAG_HRULE, "foreground", "#5C6370",
                             "justification", GTK_JUSTIFY_CENTER,
                             "pixels-above-lines", 6, "pixels-below-lines", 6,
                             NULL);
}

void markdown_update_accent_tags(GtkTextBuffer *buffer) {
  GtkTextTagTable *table;
  GtkTextTag *tag;

  if (!buffer || !config) {
    return;
  }

  table = gtk_text_buffer_get_tag_table(buffer);
  if (!table) {
    return;
  }

  tag = gtk_text_tag_table_lookup(table, TAG_H1);
  if (tag) {
    g_object_set(tag, "foreground", config->h1_color, NULL);
  }
  tag = gtk_text_tag_table_lookup(table, TAG_H2);
  if (tag) {
    g_object_set(tag, "foreground", config->h2_color, NULL);
  }
  tag = gtk_text_tag_table_lookup(table, TAG_H3);
  if (tag) {
    g_object_set(tag, "foreground", config->h3_color, NULL);
  }
  tag = gtk_text_tag_table_lookup(table, TAG_LIST_BULLET);
  if (tag) {
    g_object_set(tag, "foreground", config->list_bullet_color, NULL);
  }
}

/* Helper to check if a line matches a prefix pattern */
static gboolean line_starts_with(const gchar *line, const gchar *prefix) {
  return g_str_has_prefix(line, prefix);
}

static gboolean is_hrule_line(const gchar *line) {
  gchar *trimmed;
  gsize len;
  char c;

  if (!line) {
    return FALSE;
  }

  trimmed = g_strstrip(g_strdup(line));
  len = strlen(trimmed);
  if (len < 3) {
    g_free(trimmed);
    return FALSE;
  }

  c = trimmed[0];
  if (!(c == '-' || c == '*' || c == '_')) {
    g_free(trimmed);
    return FALSE;
  }

  for (gsize i = 1; i < len; i++) {
    if (trimmed[i] != c) {
      g_free(trimmed);
      return FALSE;
    }
  }

  g_free(trimmed);
  return TRUE;
}

static gboolean is_all_ascii_space(const gchar *s) {
  while (s && *s) {
    if (!g_ascii_isspace(*s)) {
      return FALSE;
    }
    s++;
  }
  return TRUE;
}

static gboolean is_code_fence_line(const gchar *line, gboolean in_code_block) {
  gchar *trimmed;
  const gchar *p;
  gint ticks = 0;
  gboolean result = FALSE;

  if (!line) {
    return FALSE;
  }

  trimmed = g_strstrip(g_strdup(line));
  p = trimmed;

  while (*p == '`') {
    ticks++;
    p++;
  }

  if (ticks >= 3) {
    if (in_code_block) {
      /* Closing fence: only optional whitespace after backticks. */
      result = is_all_ascii_space(p);
    } else {
      /* Opening fence: allow info string, but reject inline ```code``` form. */
      result = (strchr(p, '`') == NULL);
    }
  }

  g_free(trimmed);
  return result;
}

static void apply_tag_to_line(GtkTextBuffer *buffer, const gchar *tag_name,
                              const GtkTextIter *line_start,
                              const GtkTextIter *line_end) {
  GtkTextIter start = *line_start;
  GtkTextIter end = *line_end;

  gtk_text_buffer_apply_tag_by_name(buffer, tag_name, &start, &end);
}

/* Apply tag to range and hide syntax markers */
static void apply_tag_hide_syntax(GtkTextBuffer *buffer, const gchar *tag_name,
                                  gint content_start_offset,
                                  gint content_end_offset,
                                  gint syntax_start_offset,
                                  gint syntax_start_len, gint syntax_end_offset,
                                  gint syntax_end_len) {
  GtkTextIter start, end;

  /* Apply formatting to content */
  gtk_text_buffer_get_iter_at_offset(buffer, &start, content_start_offset);
  gtk_text_buffer_get_iter_at_offset(buffer, &end, content_end_offset);
  gtk_text_buffer_apply_tag_by_name(buffer, tag_name, &start, &end);

  /* Hide opening syntax */
  if (syntax_start_len > 0) {
    gtk_text_buffer_get_iter_at_offset(buffer, &start, syntax_start_offset);
    gtk_text_buffer_get_iter_at_offset(buffer, &end,
                                       syntax_start_offset + syntax_start_len);
    gtk_text_buffer_apply_tag_by_name(buffer, TAG_INVISIBLE, &start, &end);
  }

  /* Hide closing syntax */
  if (syntax_end_len > 0) {
    gtk_text_buffer_get_iter_at_offset(buffer, &start, syntax_end_offset);
    gtk_text_buffer_get_iter_at_offset(buffer, &end,
                                       syntax_end_offset + syntax_end_len);
    gtk_text_buffer_apply_tag_by_name(buffer, TAG_INVISIBLE, &start, &end);
  }
}

/* Apply inline formatting (bold, italic, code) */
static void apply_inline_tags(GtkTextBuffer *buffer, GtkTextIter *line_start,
                              GtkTextIter *line_end) {
  gchar *line_text;
  const gchar *p;
  gint line_offset;
  GRegex *url_re;
  GMatchInfo *url_match;

  line_text = gtk_text_buffer_get_text(buffer, line_start, line_end, FALSE);
  line_offset = gtk_text_iter_get_offset(line_start);
  p = line_text;

  while (*p) {
    /* Bold: **text** */
    if (p[0] == '*' && p[1] == '*') {
      const gchar *end = strstr(p + 2, "**");
      if (end && end > p + 2) {
        gint match_start = g_utf8_pointer_to_offset(line_text, p) + line_offset;
        gint content_start = match_start + 2;
        gint content_end = g_utf8_pointer_to_offset(line_text, end) + line_offset;

        apply_tag_hide_syntax(buffer, TAG_BOLD, content_start, content_end,
                              match_start, 2, content_end, 2);

        p = end + 2;
        continue;
      }
    }

    /* Italic: *text* (but not **) */
    if (p[0] == '*' && p[1] != '*') {
      const gchar *end = strchr(p + 1, '*');
      if (end && end > p + 1 && *(end + 1) != '*') {
        gint match_start = g_utf8_pointer_to_offset(line_text, p) + line_offset;
        gint content_start = match_start + 1;
        gint content_end = g_utf8_pointer_to_offset(line_text, end) + line_offset;

        apply_tag_hide_syntax(buffer, TAG_ITALIC, content_start, content_end,
                              match_start, 1, content_end, 1);

        p = end + 1;
        continue;
      }
    }

    /* Inline code: `text` */
    if (p[0] == '`' && p[1] != '`') {
      const gchar *end = strchr(p + 1, '`');
      if (end && end > p + 1) {
        gint match_start = g_utf8_pointer_to_offset(line_text, p) + line_offset;
        gint content_start = match_start + 1;
        gint content_end = g_utf8_pointer_to_offset(line_text, end) + line_offset;

        apply_tag_hide_syntax(buffer, TAG_CODE, content_start, content_end,
                              match_start, 1, content_end, 1);

        p = end + 1;
        continue;
      }
    }

    /* Link: [text](url) */
    if (p[0] == '[') {
      const gchar *bracket_end = strchr(p + 1, ']');
      if (bracket_end && bracket_end[1] == '(') {
        const gchar *paren_end = strchr(bracket_end + 2, ')');
        if (paren_end) {
          gint link_start = g_utf8_pointer_to_offset(line_text, p) + line_offset;
          gint text_start = link_start + 1;
          gint text_end =
              g_utf8_pointer_to_offset(line_text, bracket_end) + line_offset;
          gint url_end =
              g_utf8_pointer_to_offset(line_text, paren_end) + line_offset;

          GtkTextIter start, end;

          /* Apply link style to text */
          gtk_text_buffer_get_iter_at_offset(buffer, &start, text_start);
          gtk_text_buffer_get_iter_at_offset(buffer, &end, text_end);
          gtk_text_buffer_apply_tag_by_name(buffer, TAG_LINK, &start, &end);

          /* Hide [ */
          gtk_text_buffer_get_iter_at_offset(buffer, &start, link_start);
          gtk_text_buffer_get_iter_at_offset(buffer, &end, link_start + 1);
          gtk_text_buffer_apply_tag_by_name(buffer, TAG_INVISIBLE, &start,
                                            &end);

          /* Hide ](url) */
          gtk_text_buffer_get_iter_at_offset(buffer, &start, text_end);
          gtk_text_buffer_get_iter_at_offset(buffer, &end, url_end + 1);
          gtk_text_buffer_apply_tag_by_name(buffer, TAG_INVISIBLE, &start,
                                            &end);

          p = paren_end + 1;
          continue;
        }
      }
    }

    p++;
  }

  /* Auto-link plain URLs (e.g., https://..., www....) */
  url_re = g_regex_new(
      "\\b(https?://[^\\s<>()]+|www\\.[^\\s<>()]+)", G_REGEX_CASELESS, 0, NULL);
  if (!url_re) {
    g_free(line_text);
    return;
  }

  url_match = NULL;
  if (g_regex_match(url_re, line_text, 0, &url_match)) {
    while (g_match_info_matches(url_match)) {
      gint mstart = 0, mend = 0;
      if (g_match_info_fetch_pos(url_match, 1, &mstart, &mend)) {
        /* Trim common trailing punctuation */
        while (mend > mstart) {
          const gchar *prev =
              g_utf8_find_prev_char(line_text, line_text + mend);
          if (!prev) {
            break;
          }
          gunichar c = g_utf8_get_char(prev);
          if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!' ||
              c == '?' || c == ')' || c == ']' || c == '}' || c == '"' ||
              c == '\'') {
            mend = (gint)(prev - line_text);
            continue;
          }
          break;
        }

        if (mend > mstart) {
          GtkTextIter s, e;
          gint cstart = g_utf8_pointer_to_offset(line_text, line_text + mstart);
          gint cend = g_utf8_pointer_to_offset(line_text, line_text + mend);
          gtk_text_buffer_get_iter_at_offset(buffer, &s, line_offset + cstart);
          gtk_text_buffer_get_iter_at_offset(buffer, &e, line_offset + cend);
          gtk_text_buffer_apply_tag_by_name(buffer, TAG_LINK, &s, &e);
        }
      }
      if (!g_match_info_next(url_match, NULL)) {
        break;
      }
    }
  }

  if (url_match) {
    g_match_info_free(url_match);
  }
  g_regex_unref(url_re);

  g_free(line_text);
}

void markdown_apply_tags(GtkTextBuffer *buffer) {
  GtkTextIter start, end, line_start, line_end;
  gchar *line_text;
  GArray *hrule_offsets;
  GArray *old_anchor_offsets;
  gboolean in_code_block = FALSE;

  /*
   * GtkTextIters become invalid if we mutate the buffer (delete/insert anchors)
   * while iterating. Do all buffer mutations using collected offsets.
   */
  old_anchor_offsets = g_array_new(FALSE, FALSE, sizeof(gint));
  collect_anchor_offsets(buffer, TRAYMD_HRULE_ANCHOR_DATA, old_anchor_offsets);
  delete_char_offsets(buffer, old_anchor_offsets);
  g_array_free(old_anchor_offsets, TRUE);

  /* Remove all existing tags first */
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  gtk_text_buffer_remove_all_tags(buffer, &start, &end);

  /* Process line by line */
  gtk_text_buffer_get_start_iter(buffer, &line_start);
  hrule_offsets = g_array_new(FALSE, FALSE, sizeof(gint));

  while (!gtk_text_iter_is_end(&line_start)) {
    GtkTextIter syntax_end;
    gint line_offset = gtk_text_iter_get_offset(&line_start);

    line_end = line_start;
    gtk_text_iter_forward_to_line_end(&line_end);

    line_text = gtk_text_buffer_get_text(buffer, &line_start, &line_end, FALSE);

    if (is_code_fence_line(line_text, in_code_block)) {
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_INVISIBLE, &line_start,
                                        &line_end);
      in_code_block = !in_code_block;
    }
    /* Inside fenced code block: no markdown parsing, style whole line. */
    else if (in_code_block) {
      apply_tag_to_line(buffer, TAG_CODE_BLOCK, &line_start, &line_end);
    }
    /* Headers - hide the # symbols */
    else if (line_starts_with(line_text, "### ")) {
      /* Hide "### " */
      gtk_text_buffer_get_iter_at_offset(buffer, &syntax_end, line_offset + 4);
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_INVISIBLE, &line_start,
                                        &syntax_end);
      /* Apply header style to rest */
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_H3, &syntax_end, &line_end);
    } else if (line_starts_with(line_text, "## ")) {
      gtk_text_buffer_get_iter_at_offset(buffer, &syntax_end, line_offset + 3);
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_INVISIBLE, &line_start,
                                        &syntax_end);
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_H2, &syntax_end, &line_end);
    } else if (line_starts_with(line_text, "# ")) {
      gtk_text_buffer_get_iter_at_offset(buffer, &syntax_end, line_offset + 2);
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_INVISIBLE, &line_start,
                                        &syntax_end);
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_H1, &syntax_end, &line_end);
    }
    /* Quote - hide "> " and style the rest */
    else if (line_starts_with(line_text, "> ")) {
      gtk_text_buffer_get_iter_at_offset(buffer, &syntax_end, line_offset + 2);
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_INVISIBLE, &line_start,
                                        &syntax_end);
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_QUOTE, &syntax_end,
                                        &line_end);
    }
    /* List item - style the bullet, hide and replace with bullet character */
    else if (line_starts_with(line_text, "- ") ||
             line_starts_with(line_text, "* ") ||
             line_starts_with(line_text, "• ")) {
      /* Apply list style to bullet */
      gtk_text_buffer_get_iter_at_offset(buffer, &syntax_end, line_offset + 1);
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_LIST_BULLET, &line_start,
                                        &syntax_end);

      /* Apply list style to whole line */
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_LIST, &line_start,
                                        &line_end);
      /* Apply inline tags to content after marker */
      GtkTextIter content_start;
      gtk_text_buffer_get_iter_at_offset(buffer, &content_start,
                                         line_offset + 2);
      apply_inline_tags(buffer, &content_start, &line_end);
    }
    /* Numbered list - support 1. 2. 3. etc */
    else if (g_ascii_isdigit(line_text[0])) {
      const gchar *dot = strchr(line_text, '.');
      if (dot && dot[1] == ' ' && dot - line_text <= 3) {
        gint prefix_len = (dot - line_text) + 2;
        gtk_text_buffer_get_iter_at_offset(buffer, &syntax_end,
                                           line_offset + prefix_len);
        gtk_text_buffer_apply_tag_by_name(buffer, TAG_LIST_BULLET, &line_start,
                                          &syntax_end);
        gtk_text_buffer_apply_tag_by_name(buffer, TAG_LIST, &line_start,
                                          &line_end);
        /* Apply inline tags to content */
        apply_inline_tags(buffer, &syntax_end, &line_end);
      } else {
        apply_inline_tags(buffer, &line_start, &line_end);
      }
    }
  /* Horizontal rule */
  else if (is_hrule_line(line_text)) {
      /* Hide the markdown syntax, but leave it editable. */
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_INVISIBLE, &line_start,
                                        &line_end);

      /* Record for anchor insertion after this scan completes. */
      g_array_append_val(hrule_offsets, line_offset);
    }
    /* Regular line - apply inline formatting */
    else {
      apply_inline_tags(buffer, &line_start, &line_end);
    }

    g_free(line_text);

    /* Move to next line */
    if (!gtk_text_iter_forward_line(&line_start)) {
      break;
    }
  }

  /* Insert hrule anchors from end to start so offsets stay valid. */
  if (hrule_offsets->len > 0) {
    g_array_sort(hrule_offsets, compare_int_desc);
    for (guint i = 0; i < hrule_offsets->len; i++) {
      gint offset = g_array_index(hrule_offsets, gint, i);
      GtkTextIter anchor_pos;
      GtkTextChildAnchor *anchor;

      gtk_text_buffer_get_iter_at_offset(buffer, &anchor_pos, offset);
      anchor = gtk_text_buffer_create_child_anchor(buffer, &anchor_pos);
      g_object_set_data(G_OBJECT(anchor), TRAYMD_HRULE_ANCHOR_DATA,
                        GINT_TO_POINTER(1));

      /* Hide the anchor's object character so it doesn't show up/capture layout. */
      GtkTextIter astart = anchor_pos;
      GtkTextIter aend = anchor_pos;
      if (gtk_text_iter_forward_char(&aend)) {
        gtk_text_buffer_apply_tag_by_name(buffer, TAG_INVISIBLE, &astart, &aend);
      }
    }
  }

  g_array_free(hrule_offsets, TRUE);
}
