#include "markdown.h"
#include <ctype.h>
#include <string.h>

/* Tag names */
#define TAG_H1 "h1"
#define TAG_H2 "h2"
#define TAG_H3 "h3"
#define TAG_BOLD "bold"
#define TAG_ITALIC "italic"
#define TAG_CODE "code"
#define TAG_QUOTE "quote"
#define TAG_LIST "list"
#define TAG_LIST_BULLET "list_bullet"
#define TAG_LINK "link"
#define TAG_HRULE "hrule"
#define TAG_INVISIBLE "invisible"

void markdown_init_tags(GtkTextBuffer *buffer) {
  /* Invisible tag - hides markdown syntax characters */
  gtk_text_buffer_create_tag(buffer, TAG_INVISIBLE, "invisible", TRUE, NULL);

  /* Header 1 - Large bold */
  gtk_text_buffer_create_tag(buffer, TAG_H1, "weight", PANGO_WEIGHT_BOLD,
                             "scale", 2.0, "foreground", "#61AFEF",
                             "pixels-below-lines", 12, NULL);

  /* Header 2 - Medium bold */
  gtk_text_buffer_create_tag(buffer, TAG_H2, "weight", PANGO_WEIGHT_BOLD,
                             "scale", 1.6, "foreground", "#C678DD",
                             "pixels-below-lines", 10, NULL);

  /* Header 3 - Small bold */
  gtk_text_buffer_create_tag(buffer, TAG_H3, "weight", PANGO_WEIGHT_BOLD,
                             "scale", 1.3, "foreground", "#E5C07B",
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

  /* Quote - Indented and styled */
  gtk_text_buffer_create_tag(buffer, TAG_QUOTE, "left-margin", 24, "style",
                             PANGO_STYLE_ITALIC, "foreground", "#5C6370",
                             "paragraph-background", "#2C313A", NULL);

  /* List item - indentation */
  gtk_text_buffer_create_tag(buffer, TAG_LIST, "left-margin", 28, NULL);

  /* List bullet styling */
  gtk_text_buffer_create_tag(buffer, TAG_LIST_BULLET, "foreground", "#61AFEF",
                             NULL);

  /* Link - Blue underlined */
  gtk_text_buffer_create_tag(buffer, TAG_LINK, "foreground", "#61AFEF",
                             "underline", PANGO_UNDERLINE_SINGLE, NULL);

  /* Horizontal rule */
  gtk_text_buffer_create_tag(buffer, TAG_HRULE, "foreground", "#5C6370",
                             "justification", GTK_JUSTIFY_CENTER, NULL);
}

/* Helper to check if a line matches a prefix pattern */
static gboolean line_starts_with(const gchar *line, const gchar *prefix) {
  return g_str_has_prefix(line, prefix);
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

  line_text = gtk_text_buffer_get_text(buffer, line_start, line_end, FALSE);
  line_offset = gtk_text_iter_get_offset(line_start);
  p = line_text;

  while (*p) {
    /* Bold: **text** */
    if (p[0] == '*' && p[1] == '*') {
      const gchar *end = strstr(p + 2, "**");
      if (end && end > p + 2) {
        gint match_start = (p - line_text) + line_offset;
        gint content_start = match_start + 2;
        gint content_end = (end - line_text) + line_offset;

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
        gint match_start = (p - line_text) + line_offset;
        gint content_start = match_start + 1;
        gint content_end = (end - line_text) + line_offset;

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
        gint match_start = (p - line_text) + line_offset;
        gint content_start = match_start + 1;
        gint content_end = (end - line_text) + line_offset;

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
          gint link_start = (p - line_text) + line_offset;
          gint text_start = link_start + 1;
          gint text_end = (bracket_end - line_text) + line_offset;
          gint url_start = text_end + 2;
          gint url_end = (paren_end - line_text) + line_offset;

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

  g_free(line_text);
}

void markdown_apply_tags(GtkTextBuffer *buffer) {
  GtkTextIter start, end, line_start, line_end;
  gchar *line_text;

  /* Remove all existing tags first */
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  gtk_text_buffer_remove_all_tags(buffer, &start, &end);

  /* Process line by line */
  gtk_text_buffer_get_start_iter(buffer, &line_start);

  while (!gtk_text_iter_is_end(&line_start)) {
    GtkTextIter syntax_end;
    gint line_offset = gtk_text_iter_get_offset(&line_start);

    line_end = line_start;
    gtk_text_iter_forward_to_line_end(&line_end);

    line_text = gtk_text_buffer_get_text(buffer, &line_start, &line_end, FALSE);

    /* Headers - hide the # symbols */
    if (line_starts_with(line_text, "### ")) {
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
             line_starts_with(line_text, "* ")) {
      /* Apply list style to bullet */
      gtk_text_buffer_get_iter_at_offset(buffer, &syntax_end, line_offset + 1);
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_LIST_BULLET, &line_start,
                                        &syntax_end);
      /* Apply list style to whole line */
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_LIST, &line_start,
                                        &line_end);
      /* Apply inline tags to content after "- " */
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
    else if (g_strcmp0(line_text, "---") == 0 ||
             g_strcmp0(line_text, "***") == 0 ||
             g_strcmp0(line_text, "___") == 0) {
      gtk_text_buffer_apply_tag_by_name(buffer, TAG_HRULE, &line_start,
                                        &line_end);
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
}
