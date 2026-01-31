#include "editor.h"
#include "app.h"
#include "markdown.h"
#include <ctype.h>
#include <string.h>

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data);
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
                             gpointer user_data);

MarkydEditor *markyd_editor_new(MarkydApp *app) {
  MarkydEditor *self = g_new0(MarkydEditor, 1);

  self->app = app;
  self->updating_tags = FALSE;

  /* Create text view */
  self->text_view = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(self->text_view),
                              GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(self->text_view), 16);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(self->text_view), 16);
  gtk_text_view_set_top_margin(GTK_TEXT_VIEW(self->text_view), 16);
  gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(self->text_view), 16);

  /* Get buffer and init markdown tags */
  self->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->text_view));
  markdown_init_tags(self->buffer);

  /* Connect to buffer changes */
  g_signal_connect(self->buffer, "changed", G_CALLBACK(on_buffer_changed),
                   self);

  /* Connect to key press for list continuation */
  g_signal_connect(self->text_view, "key-press-event", G_CALLBACK(on_key_press),
                   self);

  return self;
}

void markyd_editor_free(MarkydEditor *self) {
  if (!self)
    return;
  g_free(self);
}

void markyd_editor_set_content(MarkydEditor *self, const gchar *content) {
  self->updating_tags = TRUE;
  gtk_text_buffer_set_text(self->buffer, content ? content : "", -1);
  self->updating_tags = FALSE;

  /* Apply markdown formatting */
  markdown_apply_tags(self->buffer);
}

gchar *markyd_editor_get_content(MarkydEditor *self) {
  GtkTextIter start, end;

  gtk_text_buffer_get_bounds(self->buffer, &start, &end);
  /* TRUE = include hidden chars (markdown syntax) so they're preserved when
   * saving */
  return gtk_text_buffer_get_text(self->buffer, &start, &end, TRUE);
}

GtkWidget *markyd_editor_get_widget(MarkydEditor *self) {
  return self->text_view;
}

/* Check if line is an empty list item (just the prefix with no content) */
static gboolean is_empty_list_item(const gchar *line) {
  if (!line || !*line)
    return FALSE;

  /* Unordered list: "- " or "* " with nothing after */
  if ((line[0] == '-' || line[0] == '*') && line[1] == ' ') {
    return line[2] == '\0';
  }

  /* Ordered list: "1. ", "2. ", etc. with nothing after */
  if (g_ascii_isdigit(line[0])) {
    const gchar *p = line;
    while (g_ascii_isdigit(*p))
      p++;
    if (*p == '.' && *(p + 1) == ' ') {
      return *(p + 2) == '\0';
    }
  }

  return FALSE;
}

/* Get the next list prefix for continuing a list */
static gchar *get_next_list_prefix(const gchar *line) {
  if (!line || !*line)
    return NULL;

  /* Unordered list: "- " or "* " */
  if ((line[0] == '-' || line[0] == '*') && line[1] == ' ') {
    /* Check if line has content after prefix */
    if (line[2] == '\0') {
      return NULL; /* Empty list item, signal to end list */
    }
    return g_strndup(line, 2);
  }

  /* Ordered list: "1. ", "2. ", etc. */
  if (g_ascii_isdigit(line[0])) {
    const gchar *p = line;
    gint num = 0;

    while (g_ascii_isdigit(*p)) {
      num = num * 10 + (*p - '0');
      p++;
    }

    if (*p == '.' && *(p + 1) == ' ') {
      /* Check if line has content after prefix */
      if (*(p + 2) == '\0') {
        return NULL; /* Empty list item, signal to end list */
      }
      return g_strdup_printf("%d. ", num + 1);
    }
  }

  return NULL;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
                             gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  GtkTextBuffer *buffer = self->buffer;
  GtkTextIter cursor, line_start, line_end;
  gchar *line_text;
  gchar *prefix;

  (void)widget;

  /* Only handle Return/Enter key */
  if (event->keyval != GDK_KEY_Return && event->keyval != GDK_KEY_KP_Enter) {
    return FALSE;
  }

  /* Don't handle if modifiers are pressed */
  if (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK)) {
    return FALSE;
  }

  /* Get cursor position */
  gtk_text_buffer_get_iter_at_mark(buffer, &cursor,
                                   gtk_text_buffer_get_insert(buffer));

  /* Get the FULL current line (from start to end, not just to cursor) */
  line_start = cursor;
  gtk_text_iter_set_line_offset(&line_start, 0);
  line_end = line_start;
  if (!gtk_text_iter_ends_line(&line_end)) {
    gtk_text_iter_forward_to_line_end(&line_end);
  }

  line_text = gtk_text_buffer_get_text(buffer, &line_start, &line_end, FALSE);

  /* First check: is this an empty list item? (just "- " or "1. " with no
   * content) */
  if (is_empty_list_item(line_text)) {
    /* Delete the entire line content (the empty list marker) */
    self->updating_tags = TRUE;
    gtk_text_buffer_delete(buffer, &line_start, &line_end);
    self->updating_tags = FALSE;

    /* Apply markdown formatting */
    markdown_apply_tags(buffer);

    g_free(line_text);
    return TRUE; /* Consume the event - don't add newline, just clear the marker
                  */
  }

  /* Check if we should continue a list */
  prefix = get_next_list_prefix(line_text);
  g_free(line_text);

  if (prefix) {
    /* Insert newline and the list prefix */
    self->updating_tags = TRUE;

    /* Insert at cursor position */
    gchar *insert_text = g_strdup_printf("\n%s", prefix);
    gtk_text_buffer_insert_at_cursor(buffer, insert_text, -1);
    g_free(insert_text);
    g_free(prefix);

    self->updating_tags = FALSE;

    /* Apply markdown formatting */
    markdown_apply_tags(buffer);

    /* Scroll to cursor to keep it visible */
    GtkTextMark *insert_mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(self->text_view), insert_mark,
                                 0.0,   /* within_margin */
                                 FALSE, /* use_align */
                                 0.0,   /* xalign */
                                 0.0);  /* yalign */

    return TRUE; /* Consume the event */
  }

  return FALSE; /* Let GTK handle the keypress normally */
}

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;

  if (self->updating_tags) {
    return;
  }

  /* Schedule auto-save */
  markyd_app_schedule_save(self->app);

  /* Apply markdown tags */
  self->updating_tags = TRUE;
  markdown_apply_tags(buffer);
  self->updating_tags = FALSE;
}
