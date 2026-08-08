#include "editor.h"
#include "app.h"
#include "markdown.h"
#include "window.h"
#include <ctype.h>
#include <string.h>

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data);
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
                             gpointer user_data);
static void on_text_view_size_allocate(GtkWidget *widget,
                                       GtkAllocation *allocation,
                                       gpointer user_data);
static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event,
                                  gpointer user_data);
static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event,
                                gpointer user_data);
static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event,
                                 gpointer user_data);
static gboolean on_leave_notify(GtkWidget *widget, GdkEventCrossing *event,
                                gpointer user_data);
static void on_paste_clipboard(GtkTextView *text_view, gpointer user_data);
static void on_paste_clipboard_after(GtkTextView *text_view, gpointer user_data);
static void on_insert_text(GtkTextBuffer *buffer, GtkTextIter *location,
                           gchar *text, gint len, gpointer user_data);
static void on_delete_range(GtkTextBuffer *buffer, GtkTextIter *start,
                            GtkTextIter *end, gpointer user_data);
static void on_begin_user_action(GtkTextBuffer *buffer, gpointer user_data);
static void on_end_user_action(GtkTextBuffer *buffer, gpointer user_data);
static void apply_markdown(MarkydEditor *self);
static void schedule_markdown_apply(MarkydEditor *self);

static const gunichar UNORDERED_LIST_BULLET = 0x2022; /* '•' */

static gint compare_int_desc(gconstpointer a, gconstpointer b) {
  const gint ia = *(const gint *)a;
  const gint ib = *(const gint *)b;
  return (ib - ia);
}

static gchar *gstring_steal_compat(GString *string) {
  if (!string) {
    return NULL;
  }

  gchar *out = g_strdup(string->str);
  g_string_free(string, TRUE);
  return out;
}

static gchar *markdown_to_display_text(const gchar *content) {
  if (!content) {
    return g_strdup("");
  }

  GString *out = g_string_sized_new(strlen(content));
  const gchar *p = content;
  gboolean at_line_start = TRUE;

  while (*p) {
    if (at_line_start && (p[0] == '-' || p[0] == '*') && p[1] == ' ') {
      g_string_append_unichar(out, UNORDERED_LIST_BULLET);
      g_string_append_c(out, ' ');
      p += 2;
      at_line_start = FALSE;
      continue;
    }

    gunichar c = g_utf8_get_char(p);
    g_string_append_unichar(out, c);
    at_line_start = (c == '\n');
    p = g_utf8_next_char(p);
  }

  return gstring_steal_compat(out);
}

static gchar *display_to_markdown_text(const gchar *content) {
  if (!content) {
    return g_strdup("");
  }

  GString *out = g_string_sized_new(strlen(content));
  const gchar *p = content;
  gboolean at_line_start = TRUE;

  while (*p) {
    if (at_line_start) {
      gunichar c0 = g_utf8_get_char(p);
      const gchar *p1 = g_utf8_next_char(p);
      if (c0 == UNORDERED_LIST_BULLET && *p1 == ' ') {
        g_string_append(out, "- ");
        p = p1 + 1;
        at_line_start = FALSE;
        continue;
      }
    }

    gunichar c = g_utf8_get_char(p);
    g_string_append_unichar(out, c);
    at_line_start = (c == '\n');
    p = g_utf8_next_char(p);
  }

  return gstring_steal_compat(out);
}

static gboolean hr_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
  (void)user_data;

  GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
  GdkRGBA color;
  gtk_style_context_get_color(ctx, GTK_STATE_FLAG_NORMAL, &color);
  color.alpha = MIN(color.alpha, 0.35);

  const gint width = gtk_widget_get_allocated_width(widget);
  const gint height = gtk_widget_get_allocated_height(widget);

  gdk_cairo_set_source_rgba(cr, &color);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, 0, height / 2.0);
  cairo_line_to(cr, width, height / 2.0);
  cairo_stroke(cr);

  return FALSE;
}

static const gint HR_WIDGET_HEIGHT_PX = 22;
static const gchar *HR_WIDGET_DATA_KEY = "traymd-hr-widget";

static void clear_last_paste(MarkydEditor *self) {
  if (!self) {
    return;
  }

  if (self->paste_inserted_start) {
    gtk_text_buffer_delete_mark(self->buffer, self->paste_inserted_start);
    self->paste_inserted_start = NULL;
  }
  if (self->paste_inserted_end) {
    gtk_text_buffer_delete_mark(self->buffer, self->paste_inserted_end);
    self->paste_inserted_end = NULL;
  }

  g_clear_pointer(&self->paste_replaced_text, g_free);
  g_clear_pointer(&self->paste_clipboard_text, g_free);
  self->paste_valid = FALSE;
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
  if (strchr(line, '\n') != NULL || strchr(line, '\r') != NULL) {
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
      result = is_all_ascii_space(p);
    } else {
      result = (strchr(p, '`') == NULL);
    }
  }

  g_free(trimmed);
  return result;
}

static gboolean is_iter_inside_code_block(GtkTextBuffer *buffer,
                                          const GtkTextIter *iter) {
  GtkTextIter scan_line_start;
  GtkTextIter line_start;
  gboolean in_code_block = FALSE;

  if (!buffer || !iter) {
    return FALSE;
  }

  scan_line_start = *iter;
  gtk_text_iter_set_line_offset(&scan_line_start, 0);
  gtk_text_buffer_get_start_iter(buffer, &line_start);

  while (gtk_text_iter_compare(&line_start, &scan_line_start) <= 0) {
    GtkTextIter line_end = line_start;
    gchar *line_text;

    gtk_text_iter_forward_to_line_end(&line_end);
    line_text = gtk_text_buffer_get_text(buffer, &line_start, &line_end, TRUE);
    if (is_code_fence_line(line_text, in_code_block)) {
      in_code_block = !in_code_block;
    }
    g_free(line_text);

    if (gtk_text_iter_equal(&line_start, &scan_line_start) ||
        !gtk_text_iter_forward_line(&line_start)) {
      break;
    }
  }

  return in_code_block;
}

static void normalize_list_markers(MarkydEditor *self) {
  GtkTextIter line_start, end;
  GArray *offsets = g_array_new(FALSE, FALSE, sizeof(gint));
  gboolean in_code_block = FALSE;

  gtk_text_buffer_get_bounds(self->buffer, &line_start, &end);
  gtk_text_iter_set_line_offset(&line_start, 0);

  while (!gtk_text_iter_is_end(&line_start)) {
    gint offset = gtk_text_iter_get_offset(&line_start);
    GtkTextIter line_end = line_start;
    gchar *line_text;

    gtk_text_iter_forward_to_line_end(&line_end);
    line_text = gtk_text_buffer_get_text(self->buffer, &line_start, &line_end,
                                         TRUE);
    if (is_code_fence_line(line_text, in_code_block)) {
      in_code_block = !in_code_block;
    } else if (!in_code_block && line_text[0] != '\0' &&
               (line_text[0] == '-' || line_text[0] == '*') &&
               line_text[1] == ' ') {
      g_array_append_val(offsets, offset);
    }
    g_free(line_text);

    if (!gtk_text_iter_forward_line(&line_start)) {
      break;
    }
  }

  if (offsets->len > 0) {
    g_array_sort(offsets, compare_int_desc);
    for (guint i = 0; i < offsets->len; i++) {
      gint offset = g_array_index(offsets, gint, i);
      GtkTextIter start, finish;
      gtk_text_buffer_get_iter_at_offset(self->buffer, &start, offset);
      finish = start;
      if (gtk_text_iter_forward_chars(&finish, 2)) {
        gtk_text_buffer_delete(self->buffer, &start, &finish);
        gtk_text_buffer_insert(self->buffer, &start, "• ", -1);
      }
    }
  }

  g_array_free(offsets, TRUE);
}

static gboolean get_link_url_at_iter(GtkTextBuffer *buffer, GtkTextIter *at,
                                     gchar **out_url) {
  GtkTextTagTable *table;
  GtkTextTag *tag;
  GtkTextIter start;
  GtkTextIter end;
  GtkTextIter line_end;
  gchar *tail;
  gchar *url = NULL;

  if (!buffer || !at || !out_url) {
    return FALSE;
  }
  *out_url = NULL;

  table = gtk_text_buffer_get_tag_table(buffer);
  tag = gtk_text_tag_table_lookup(table, "link");
  if (!tag) {
    return FALSE;
  }
  if (!gtk_text_iter_has_tag(at, tag)) {
    return FALSE;
  }

  start = *at;
  end = *at;
  gtk_text_iter_backward_to_tag_toggle(&start, tag);
  gtk_text_iter_forward_to_tag_toggle(&end, tag);

  line_end = end;
  if (!gtk_text_iter_ends_line(&line_end)) {
    gtk_text_iter_forward_to_line_end(&line_end);
  }

  /*
   * Markdown link: the URL is right after the visible link text: ](url)
   * Auto-link: the visible text itself is the URL.
   */
  tail = gtk_text_buffer_get_text(buffer, &end, &line_end, TRUE);
  if (tail) {
    GRegex *re = g_regex_new("^\\]\\(([^)]+)\\)", 0, 0, NULL);
    GMatchInfo *match = NULL;
    if (re && g_regex_match(re, tail, 0, &match)) {
      url = g_match_info_fetch(match, 1);
    }
    if (match) {
      g_match_info_free(match);
    }
    if (re) {
      g_regex_unref(re);
    }
    g_free(tail);
  }

  if (!url || url[0] == '\0') {
    g_free(url);
    url = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
  }

  if (!url || url[0] == '\0') {
    g_free(url);
    return FALSE;
  }

  *out_url = url;
  return TRUE;
}

static void set_link_cursor(MarkydEditor *self, gboolean active) {
  GdkWindow *win = gtk_text_view_get_window(GTK_TEXT_VIEW(self->text_view),
                                            GTK_TEXT_WINDOW_TEXT);
  if (!win) {
    return;
  }

  GdkDisplay *display = gdk_window_get_display(win);
  GdkCursor *cursor;

  if (active) {
    cursor = gdk_cursor_new_from_name(display, "pointer");
    if (!cursor) {
      cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
    }
  } else {
    cursor = gdk_cursor_new_from_name(display, "text");
    if (!cursor) {
      cursor = gdk_cursor_new_for_display(display, GDK_XTERM);
    }
  }

  gdk_window_set_cursor(win, cursor);
  if (cursor) {
    g_object_unref(cursor);
  }
}

static void render_hrules(MarkydEditor *self) {
  GtkTextIter iter, end;

  gtk_text_buffer_get_bounds(self->buffer, &iter, &end);
  while (!gtk_text_iter_equal(&iter, &end)) {
    GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&iter);
    if (anchor) {
      if (g_object_get_data(G_OBJECT(anchor), TRAYMD_HRULE_ANCHOR_DATA) !=
          NULL) {
        GtkWidget *hr = g_object_get_data(G_OBJECT(anchor), HR_WIDGET_DATA_KEY);
        if (!hr) {
          hr = gtk_drawing_area_new();
          g_signal_connect(hr, "draw", G_CALLBACK(hr_draw), NULL);
          gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(self->text_view), hr,
                                            anchor);
          gtk_widget_show(hr);
          g_object_set_data(G_OBJECT(anchor), HR_WIDGET_DATA_KEY, hr);
        }
        gtk_widget_set_size_request(hr, 1, HR_WIDGET_HEIGHT_PX);
      }
    }
    gtk_text_iter_forward_char(&iter);
  }

  /* Ensure hr widgets get the right width after creation. */
  GtkAllocation allocation;
  gtk_widget_get_allocation(self->text_view, &allocation);
  on_text_view_size_allocate(self->text_view, &allocation, self);
}

static void apply_markdown(MarkydEditor *self) {
  if (!self) {
    return;
  }

  /*
   * Rendering rewrites the buffer: list markers are normalised and the child
   * anchors carrying horizontal rules are dropped and re-created. None of that
   * is a user edit, so keep it out of the history.
   */
  self->updating_tags = TRUE;
  self->undo_suppress = TRUE;
  normalize_list_markers(self);
  markdown_apply_tags(self->buffer);
  render_hrules(self);
  self->undo_suppress = FALSE;
  self->updating_tags = FALSE;
}

static gboolean apply_markdown_idle(gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  self->markdown_idle_id = 0;
  apply_markdown(self);
  return G_SOURCE_REMOVE;
}

static void schedule_markdown_apply(MarkydEditor *self) {
  if (!self) {
    return;
  }
  if (self->updating_tags) {
    return;
  }
  if (self->markdown_idle_id != 0) {
    return;
  }

  self->markdown_idle_id =
      g_idle_add_full(G_PRIORITY_LOW, apply_markdown_idle, self, NULL);
}

void markyd_editor_refresh(MarkydEditor *self) { schedule_markdown_apply(self); }

MarkydEditor *markyd_editor_new(MarkydApp *app) {
  MarkydEditor *self = g_new0(MarkydEditor, 1);

  self->app = app;
  self->updating_tags = FALSE;
  self->markdown_idle_id = 0;
  self->undo = markyd_undo_new();
  self->undo_suppress = FALSE;
  self->in_paste = FALSE;
  self->in_undo = FALSE;
  self->pending_paste_finalize = FALSE;
  self->paste_start_offset = 0;
  self->paste_end_offset_before = 0;
  self->paste_replaced_text = NULL;
  self->paste_clipboard_text = NULL;
  self->paste_inserted_start = NULL;
  self->paste_inserted_end = NULL;
  self->paste_valid = FALSE;
  self->paste_had_selection = FALSE;
  self->paste_sel_start_offset = 0;
  self->paste_sel_end_offset = 0;

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

  /* Record edits for undo. Both run before the buffer is mutated, so the
   * snapshot they take is the state to come back to. */
  g_signal_connect(self->buffer, "insert-text", G_CALLBACK(on_insert_text),
                   self);
  g_signal_connect(self->buffer, "delete-range", G_CALLBACK(on_delete_range),
                   self);

  g_signal_connect(self->buffer, "begin-user-action",
                   G_CALLBACK(on_begin_user_action), self);
  g_signal_connect(self->buffer, "end-user-action",
                   G_CALLBACK(on_end_user_action), self);

  /* Connect to key press for list continuation */
  g_signal_connect(self->text_view, "key-press-event", G_CALLBACK(on_key_press),
                   self);

  g_signal_connect(self->text_view, "size-allocate",
                   G_CALLBACK(on_text_view_size_allocate), self);

  /* Link hover/click */
  gtk_widget_add_events(self->text_view, GDK_POINTER_MOTION_MASK |
                                            GDK_LEAVE_NOTIFY_MASK |
                                            GDK_BUTTON_PRESS_MASK |
                                            GDK_BUTTON_RELEASE_MASK);
  g_signal_connect(self->text_view, "button-press-event",
                   G_CALLBACK(on_button_press), self);
  g_signal_connect(self->text_view, "button-release-event",
                   G_CALLBACK(on_button_release), self);
  g_signal_connect(self->text_view, "motion-notify-event",
                   G_CALLBACK(on_motion_notify), self);
  g_signal_connect(self->text_view, "leave-notify-event",
                   G_CALLBACK(on_leave_notify), self);

  /* Track pastes so we can undo the last one with Ctrl+Z */
  g_signal_connect(self->text_view, "paste-clipboard",
                   G_CALLBACK(on_paste_clipboard), self);
  g_signal_connect_after(self->text_view, "paste-clipboard",
                         G_CALLBACK(on_paste_clipboard_after), self);

  /* Set initial cursor to text (I-beam) */
  {
    GdkWindow *win = gtk_text_view_get_window(GTK_TEXT_VIEW(self->text_view),
                                              GTK_TEXT_WINDOW_TEXT);
    if (win) {
      GdkDisplay *display = gdk_window_get_display(win);
      GdkCursor *cursor = gdk_cursor_new_from_name(display, "text");
      if (!cursor) {
        cursor = gdk_cursor_new_for_display(display, GDK_XTERM);
      }
      gdk_window_set_cursor(win, cursor);
      if (cursor) {
        g_object_unref(cursor);
      }
    }
  }

  return self;
}

void markyd_editor_free(MarkydEditor *self) {
  if (!self)
    return;
  if (self->markdown_idle_id != 0) {
    g_source_remove(self->markdown_idle_id);
    self->markdown_idle_id = 0;
  }
  clear_last_paste(self);
  markyd_undo_free(self->undo);
  g_free(self);
}

void markyd_editor_set_content(MarkydEditor *self, const gchar *content) {
  gchar *display = markdown_to_display_text(content);

  /* A different note means a different history. */
  markyd_undo_clear(self->undo);

  self->updating_tags = TRUE;
  self->undo_suppress = TRUE;
  gtk_text_buffer_set_text(self->buffer, display ? display : "", -1);
  self->undo_suppress = FALSE;
  self->updating_tags = FALSE;
  g_free(display);

  /* Apply markdown formatting */
  schedule_markdown_apply(self);
}

gchar *markyd_editor_get_content(MarkydEditor *self) {
  GtkTextIter start, end;
  GString *out;
  GtkTextIter iter;
  GtkTextIter run_start;
  gchar *raw;
  gchar *converted;

  gtk_text_buffer_get_bounds(self->buffer, &start, &end);

  /*
   * TRUE = include hidden chars (markdown syntax) so they're preserved when
   * saving, but skip embedded widget anchors (e.g., horizontal rules). Copy
   * the runs between anchors rather than character by character: the undo
   * history reads this on every step, and a per-character copy would show up
   * as typing latency on a long note.
   */
  out = g_string_new(NULL);
  run_start = start;
  iter = start;
  while (!gtk_text_iter_equal(&iter, &end)) {
    if (gtk_text_iter_get_child_anchor(&iter)) {
      gchar *run =
          gtk_text_buffer_get_text(self->buffer, &run_start, &iter, TRUE);
      g_string_append(out, run);
      g_free(run);

      run_start = iter;
      gtk_text_iter_forward_char(&run_start);
    }

    if (!gtk_text_iter_forward_char(&iter)) {
      break;
    }
  }

  {
    gchar *run = gtk_text_buffer_get_text(self->buffer, &run_start, &end, TRUE);
    g_string_append(out, run);
    g_free(run);
  }

  raw = gstring_steal_compat(out);
  converted = display_to_markdown_text(raw);
  g_free(raw);
  return converted;
}

GtkWidget *markyd_editor_get_widget(MarkydEditor *self) {
  return self->text_view;
}

void markyd_editor_focus(MarkydEditor *self) {
  GtkTextMark *insert_mark;

  if (!self || !self->text_view || !self->buffer) {
    return;
  }

  gtk_widget_grab_focus(self->text_view);
  insert_mark = gtk_text_buffer_get_insert(self->buffer);
  if (insert_mark) {
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(self->text_view), insert_mark,
                                 0.0, FALSE, 0.0, 0.0);
  }
}

/*
 * The buffer carries one invisible child anchor per horizontal rule, the
 * markdown text does not. The history is kept in markdown offsets so a step
 * survives a re-render moving those anchors around.
 */
static gint editor_markdown_offset(MarkydEditor *self, const GtkTextIter *at) {
  GtkTextIter iter;
  gint offset = 0;

  gtk_text_buffer_get_start_iter(self->buffer, &iter);
  while (gtk_text_iter_compare(&iter, at) < 0) {
    if (!gtk_text_iter_get_child_anchor(&iter)) {
      offset++;
    }
    if (!gtk_text_iter_forward_char(&iter)) {
      break;
    }
  }

  return offset;
}

static void editor_iter_at_markdown_offset(MarkydEditor *self, gint offset,
                                           GtkTextIter *out) {
  GtkTextIter iter;

  gtk_text_buffer_get_start_iter(self->buffer, &iter);
  while (offset > 0) {
    if (!gtk_text_iter_get_child_anchor(&iter)) {
      offset--;
    }
    if (!gtk_text_iter_forward_char(&iter)) {
      break;
    }
  }

  /* Land on the character itself, never between an anchor and its rule. */
  while (gtk_text_iter_get_child_anchor(&iter)) {
    if (!gtk_text_iter_forward_char(&iter)) {
      break;
    }
  }

  *out = iter;
}

static gint editor_cursor_markdown_offset(MarkydEditor *self) {
  GtkTextIter cursor;

  gtk_text_buffer_get_iter_at_mark(self->buffer, &cursor,
                                   gtk_text_buffer_get_insert(self->buffer));
  return editor_markdown_offset(self, &cursor);
}

static void editor_push_undo_step(MarkydEditor *self) {
  gchar *markdown = markyd_editor_get_content(self);

  markyd_undo_push(self->undo, markdown, editor_cursor_markdown_offset(self));
  g_free(markdown);
}

static void editor_record_edit(MarkydEditor *self, gboolean insert, gint start,
                               gint end, gboolean whitespace) {
  gboolean opens;

  if (self->undo_suppress) {
    return;
  }

  /* Keep the grouping state current even for an edit we won't snapshot, so
   * the one after it still sees where the last edit left off. */
  opens = markyd_undo_needs_step(self->undo, insert, start, end, whitespace);

  /*
   * Replacing a selection - by typing, pasting or dropping over it - reaches
   * the buffer as a delete followed by an insert inside one user action. Only
   * the first half may open a step, or Ctrl+Z would stop at the half-finished
   * state, with the old text already gone and the new text not yet there.
   */
  if (self->user_action_depth > 0) {
    if (self->user_action_recorded) {
      return;
    }
    self->user_action_recorded = TRUE;
  }

  if (opens) {
    editor_push_undo_step(self);
  }
}

static void on_insert_text(GtkTextBuffer *buffer, GtkTextIter *location,
                           gchar *text, gint len, gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  gint start;
  gint chars;
  gboolean whitespace;

  (void)buffer;

  chars = (text && len > 0) ? (gint)g_utf8_strlen(text, len) : 0;
  whitespace = (chars == 1) && g_unichar_isspace(g_utf8_get_char(text));
  start = gtk_text_iter_get_offset(location);

  editor_record_edit(self, TRUE, start, start + chars, whitespace);
}

static void on_delete_range(GtkTextBuffer *buffer, GtkTextIter *start,
                            GtkTextIter *end, gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  gint start_offset;
  gint end_offset;
  gboolean whitespace;

  (void)buffer;

  start_offset = gtk_text_iter_get_offset(start);
  end_offset = gtk_text_iter_get_offset(end);
  whitespace = (end_offset - start_offset == 1) &&
               g_unichar_isspace(gtk_text_iter_get_char(start));

  editor_record_edit(self, FALSE, start_offset, end_offset, whitespace);
}

/*
 * GTK nests these: deleting the selection opens a user action of its own
 * inside the one wrapping the whole keystroke or paste. Counting the depth
 * keeps the replace-selection pair in a single undo step.
 */
static void on_begin_user_action(GtkTextBuffer *buffer, gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;

  (void)buffer;

  if (self->user_action_depth++ == 0) {
    self->user_action_recorded = FALSE;
  }
}

static void on_end_user_action(GtkTextBuffer *buffer, gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;

  (void)buffer;

  if (self->user_action_depth > 0) {
    self->user_action_depth--;
  }
}

/*
 * Put the note back to "markdown" by replacing only the run of text that
 * actually differs. Rewriting the whole buffer would reset the scroll position
 * and strip every tag, which is exactly what an undo must not do.
 */
static void editor_restore(MarkydEditor *self, const gchar *current,
                           const gchar *markdown, gint cursor) {
  gsize current_len = strlen(current);
  gsize markdown_len = strlen(markdown);
  gsize shortest = MIN(current_len, markdown_len);
  gsize head = 0;
  gsize tail = 0;
  gint replace_start;
  gint replace_end;
  GtkTextIter start, end;

  while (head < shortest && current[head] == markdown[head]) {
    head++;
  }
  while (tail < shortest - head &&
         current[current_len - 1 - tail] == markdown[markdown_len - 1 - tail]) {
    tail++;
  }

  /* Both ends have to sit on a character boundary, never inside a UTF-8
   * sequence. Backing off keeps the two strings in agreement. */
  while (head > 0 && (current[head] & 0xC0) == 0x80) {
    head--;
  }
  while (tail > 0 && (current[current_len - tail] & 0xC0) == 0x80) {
    tail--;
  }

  replace_start = (gint)g_utf8_strlen(current, (gssize)head);
  replace_end =
      replace_start +
      (gint)g_utf8_strlen(current + head, (gssize)(current_len - tail - head));

  self->undo_suppress = TRUE;
  editor_iter_at_markdown_offset(self, replace_start, &start);
  editor_iter_at_markdown_offset(self, replace_end, &end);
  gtk_text_buffer_delete(self->buffer, &start, &end);
  gtk_text_buffer_insert(self->buffer, &start, markdown + head,
                         (gint)(markdown_len - tail - head));
  self->undo_suppress = FALSE;

  /* Before re-rendering: the renderer looks at the cursor line to decide
   * which horizontal rule to leave in its editable form. */
  editor_iter_at_markdown_offset(self, cursor, &start);
  gtk_text_buffer_place_cursor(self->buffer, &start);

  /* Render in this same main loop iteration, so no frame ever shows the raw
   * markdown the splice just put in. */
  if (self->markdown_idle_id != 0) {
    g_source_remove(self->markdown_idle_id);
    self->markdown_idle_id = 0;
  }
  apply_markdown(self);

  /* Follow the cursor only when it ended up out of sight. */
  gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(self->text_view),
                                     gtk_text_buffer_get_insert(self->buffer));
}

static void editor_history_step(MarkydEditor *self, gboolean redo) {
  gchar *current;
  gchar *markdown = NULL;
  gint cursor = 0;
  gboolean moved;

  current = markyd_editor_get_content(self);
  moved = redo ? markyd_undo_redo(self->undo, current,
                                  editor_cursor_markdown_offset(self),
                                  &markdown, &cursor)
               : markyd_undo_undo(self->undo, current,
                                  editor_cursor_markdown_offset(self),
                                  &markdown, &cursor);

  if (moved) {
    editor_restore(self, current, markdown, cursor);
    g_free(markdown);
  }

  g_free(current);
}

/*
 * Ctrl shortcuts have to match the physical key rather than the character it
 * produces: on a Cyrillic layout the Z key reports Cyrillic_ya, and unlike
 * Ctrl+C or Ctrl+V there is no GTK default binding to fall back on. Check
 * every group the keycode maps to, since the Latin layout is not necessarily
 * the first one.
 */
static gboolean key_event_is(GdkEventKey *event, guint latin_keyval) {
  GdkKeymap *keymap;
  GdkKeymapKey *keys = NULL;
  guint *keyvals = NULL;
  gint n_entries = 0;
  guint latin_upper = gdk_keyval_to_upper(latin_keyval);
  gboolean found = FALSE;

  if (event->keyval == latin_keyval || event->keyval == latin_upper) {
    return TRUE;
  }

  /*
   * A Latin letter came out of the layout, just not the one asked for, so the
   * layout is answer enough. Looking further would break QWERTZ, where Z and
   * Y swap places and scanning every group makes both keys answer to Ctrl+Z.
   */
  if (event->keyval < 0x80 && g_ascii_isalpha((gchar)event->keyval)) {
    return FALSE;
  }

  if (!event->window) {
    return FALSE;
  }

  keymap = gdk_keymap_get_for_display(gdk_window_get_display(event->window));
  if (!keymap) {
    return FALSE;
  }

  if (!gdk_keymap_get_entries_for_keycode(keymap, event->hardware_keycode,
                                          &keys, &keyvals, &n_entries)) {
    return FALSE;
  }

  for (gint i = 0; i < n_entries; i++) {
    if (keyvals[i] == latin_keyval || keyvals[i] == latin_upper) {
      found = TRUE;
      break;
    }
  }

  g_free(keys);
  g_free(keyvals);

  return found;
}

/* Check if line is an empty list item (just the prefix with no content) */
static gboolean is_empty_list_item(const gchar *line) {
  if (!line || !*line)
    return FALSE;

  const gchar *bullet_prefix = "• ";
  const gsize bullet_prefix_len = strlen(bullet_prefix);

  /* Unordered list: "- " or "* " with nothing after */
  if ((line[0] == '-' || line[0] == '*') && line[1] == ' ') {
    return line[2] == '\0';
  }
  /* Unordered list (display): "• " with nothing after */
  if (g_str_has_prefix(line, bullet_prefix)) {
    return strlen(line) == bullet_prefix_len;
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

  const gchar *bullet_prefix = "• ";
  const gsize bullet_prefix_len = strlen(bullet_prefix);

  /* Unordered list: "- " or "* " */
  if ((line[0] == '-' || line[0] == '*') && line[1] == ' ') {
    /* Check if line has content after prefix */
    if (line[2] == '\0') {
      return NULL; /* Empty list item, signal to end list */
    }
    return g_strndup(line, 2);
  }
  /* Unordered list (display): "• " */
  if (g_str_has_prefix(line, bullet_prefix)) {
    if (strlen(line) == bullet_prefix_len) {
      return NULL;
    }
    return g_strdup(bullet_prefix);
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

  if (event && event->keyval == GDK_KEY_Escape) {
    if (self->app && self->app->window) {
      markyd_window_close_to_tray(self->app->window);
      return TRUE;
    }
  }

  /* Ctrl+V: tracked paste so Ctrl+Z can undo it (single level). */
  if ((event->state & GDK_CONTROL_MASK) &&
      (event->keyval == GDK_KEY_v || event->keyval == GDK_KEY_V)) {
    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gchar *clip = gtk_clipboard_wait_for_text(cb);
    GtkTextIter sel_start, sel_end;
    gboolean had_selection =
        gtk_text_buffer_get_selection_bounds(buffer, &sel_start, &sel_end);

    if (!clip) {
      return FALSE;
    }

    self->in_paste = TRUE;
    clear_last_paste(self);

    self->paste_had_selection = had_selection;
    if (had_selection) {
      self->paste_sel_start_offset = gtk_text_iter_get_offset(&sel_start);
      self->paste_sel_end_offset = gtk_text_iter_get_offset(&sel_end);
      self->paste_replaced_text =
          gtk_text_buffer_get_text(buffer, &sel_start, &sel_end, TRUE);
    } else {
      gtk_text_buffer_get_iter_at_mark(buffer, &cursor,
                                       gtk_text_buffer_get_insert(buffer));
      self->paste_sel_start_offset = gtk_text_iter_get_offset(&cursor);
      self->paste_sel_end_offset = self->paste_sel_start_offset;
      self->paste_replaced_text = g_strdup("");
      sel_start = cursor;
      sel_end = cursor;
    }

    /* Mark start of insertion, then delete selection and insert clipboard text. */
    self->paste_inserted_start =
        gtk_text_buffer_create_mark(buffer, NULL, &sel_start, TRUE);

    if (had_selection) {
      gtk_text_buffer_delete(buffer, &sel_start, &sel_end);
      gtk_text_buffer_get_iter_at_mark(buffer, &sel_start,
                                       self->paste_inserted_start);
    }

    gtk_text_buffer_insert(buffer, &sel_start, clip, -1);
    self->paste_inserted_end =
        gtk_text_buffer_create_mark(buffer, NULL, &sel_start, FALSE);

    gtk_text_buffer_place_cursor(buffer, &sel_start);

    self->paste_clipboard_text = clip; /* keep for debug/inspection */
    self->paste_valid = TRUE;
    self->in_paste = FALSE;

    schedule_markdown_apply(self);
    return TRUE;
  }

  /* Ctrl+Z undoes, Ctrl+Shift+Z and Ctrl+Y redo. Alt has to be clear so
   * Ctrl+Alt+Z isn't swallowed here. */
  if ((event->state & GDK_CONTROL_MASK) && !(event->state & GDK_MOD1_MASK)) {
    if (key_event_is(event, GDK_KEY_z)) {
      editor_history_step(self, (event->state & GDK_SHIFT_MASK) != 0);
      return TRUE;
    }
    if (key_event_is(event, GDK_KEY_y)) {
      editor_history_step(self, TRUE);
      return TRUE;
    }
  }

  /* Ctrl+Z: undo last paste (single level) */
  if ((event->state & GDK_CONTROL_MASK) &&
      (event->keyval == GDK_KEY_z || event->keyval == GDK_KEY_Z)) {
    if (self->paste_valid && self->paste_inserted_start &&
        self->paste_inserted_end) {
      GtkTextIter start, end;
      GtkTextIter restore_start;

      self->in_undo = TRUE;
      self->updating_tags = TRUE;

      gtk_text_buffer_get_iter_at_mark(buffer, &start, self->paste_inserted_start);
      gtk_text_buffer_get_iter_at_mark(buffer, &end, self->paste_inserted_end);
      restore_start = start;

      gtk_text_buffer_delete(buffer, &start, &end);

      if (self->paste_replaced_text && self->paste_replaced_text[0] != '\0') {
        gtk_text_buffer_insert(buffer, &restore_start, self->paste_replaced_text,
                               -1);
      }

      /* Restore selection/cursor */
      if (self->paste_had_selection) {
        GtkTextIter sel_a, sel_b;
        gtk_text_buffer_get_iter_at_offset(buffer, &sel_a,
                                           self->paste_sel_start_offset);
        gtk_text_buffer_get_iter_at_offset(buffer, &sel_b,
                                           self->paste_sel_end_offset);
        gtk_text_buffer_select_range(buffer, &sel_a, &sel_b);
      } else {
        gtk_text_buffer_place_cursor(buffer, &restore_start);
      }

      self->updating_tags = FALSE;
      self->in_undo = FALSE;

      clear_last_paste(self);
      schedule_markdown_apply(self);
      return TRUE;
    }
    return FALSE;
  }

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

  line_text = gtk_text_buffer_get_text(buffer, &line_start, &line_end, TRUE);

  gboolean inside_code = is_iter_inside_code_block(buffer, &cursor);
  gboolean on_fence_line = is_code_fence_line(line_text, FALSE);
  gboolean cursor_at_line_end = gtk_text_iter_equal(&cursor, &line_end);

  if (cursor_at_line_end && on_fence_line) {
    gint insert_start_off = gtk_text_iter_get_offset(&cursor);
    GtkTextIter insert_pos = cursor;
    GtkTextIter insert_start;
    GtkTextIter insert_end;
    self->updating_tags = TRUE;
    gtk_text_buffer_insert(buffer, &insert_pos, "\n", 1);
    gtk_text_buffer_get_iter_at_offset(buffer, &insert_start, insert_start_off);
    gtk_text_buffer_get_iter_at_offset(buffer, &insert_end, insert_start_off + 1);
    gtk_text_buffer_remove_all_tags(buffer, &insert_start, &insert_end);
    gtk_text_buffer_place_cursor(buffer, &insert_end);
    self->updating_tags = FALSE;

    if (self->markdown_idle_id != 0) {
      g_source_remove(self->markdown_idle_id);
      self->markdown_idle_id = 0;
    }
    apply_markdown(self);
    gtk_widget_queue_draw(self->text_view);
    g_free(line_text);
    return TRUE;
  }

  if (inside_code || on_fence_line) {
    g_free(line_text);
    return FALSE;
  }

  /* First check: is this an empty list item? (just "- " or "1. " with no
   * content) */
  if (is_empty_list_item(line_text)) {
    /* Delete the entire line content (the empty list marker) */
    self->updating_tags = TRUE;
    gtk_text_buffer_delete(buffer, &line_start, &line_end);
    self->updating_tags = FALSE;

    /* Apply markdown formatting */
    schedule_markdown_apply(self);

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
    schedule_markdown_apply(self);

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
  (void)buffer;

  if (self->updating_tags) {
    return;
  }

  /* Any edit after a paste invalidates our one-level "undo paste". */
  if (self->paste_valid && !self->in_paste && !self->in_undo &&
      !self->pending_paste_finalize) {
    clear_last_paste(self);
  }

  /* Schedule auto-save */
  markyd_app_schedule_save(self->app);

  /* Apply markdown tags (deferred to avoid invalidating GTK iterators). */
  schedule_markdown_apply(self);
}

static void on_paste_clipboard(GtkTextView *text_view, gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  GtkTextBuffer *buffer = self->buffer;
  GtkTextIter sel_start, sel_end;
  GtkTextIter insert_iter;

  (void)text_view;

  self->in_paste = TRUE;
  clear_last_paste(self);

  self->paste_had_selection =
      gtk_text_buffer_get_selection_bounds(buffer, &sel_start, &sel_end);
  if (self->paste_had_selection) {
    self->paste_sel_start_offset = gtk_text_iter_get_offset(&sel_start);
    self->paste_sel_end_offset = gtk_text_iter_get_offset(&sel_end);
    self->paste_start_offset = self->paste_sel_start_offset;
    self->paste_end_offset_before = self->paste_sel_end_offset;
    self->paste_replaced_text =
        gtk_text_buffer_get_text(buffer, &sel_start, &sel_end, TRUE);
  } else {
    gtk_text_buffer_get_iter_at_mark(buffer, &insert_iter,
                                     gtk_text_buffer_get_insert(buffer));
    self->paste_start_offset = gtk_text_iter_get_offset(&insert_iter);
    self->paste_end_offset_before = self->paste_start_offset;
    self->paste_replaced_text = g_strdup("");
    self->paste_sel_start_offset = self->paste_start_offset;
    self->paste_sel_end_offset = self->paste_start_offset;
  }

  GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  self->paste_clipboard_text = gtk_clipboard_wait_for_text(cb);
  self->pending_paste_finalize = TRUE;
}

static void on_paste_clipboard_after(GtkTextView *text_view, gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  GtkTextBuffer *buffer = self->buffer;
  gint inserted_chars;
  GtkTextIter start_iter, end_iter;

  (void)text_view;

  if (!self->pending_paste_finalize) {
    self->in_paste = FALSE;
    return;
  }
  self->pending_paste_finalize = FALSE;

  if (!self->paste_clipboard_text) {
    self->in_paste = FALSE;
    return;
  }

  inserted_chars = g_utf8_strlen(self->paste_clipboard_text, -1);
  gtk_text_buffer_get_iter_at_offset(buffer, &start_iter, self->paste_start_offset);
  gtk_text_buffer_get_iter_at_offset(buffer, &end_iter,
                                     self->paste_start_offset + inserted_chars);

  self->paste_inserted_start =
      gtk_text_buffer_create_mark(buffer, NULL, &start_iter, TRUE);
  self->paste_inserted_end =
      gtk_text_buffer_create_mark(buffer, NULL, &end_iter, FALSE);

  self->paste_valid = TRUE;
  self->in_paste = FALSE;
}

static void on_text_view_size_allocate(GtkWidget *widget,
                                       GtkAllocation *allocation,
                                       gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  (void)widget;

  gint width = allocation->width;
  width -= gtk_text_view_get_left_margin(GTK_TEXT_VIEW(self->text_view));
  width -= gtk_text_view_get_right_margin(GTK_TEXT_VIEW(self->text_view));
  width = MAX(width, 1);

  GtkTextIter iter, end;
  gtk_text_buffer_get_bounds(self->buffer, &iter, &end);
  while (!gtk_text_iter_equal(&iter, &end)) {
    GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&iter);
    if (anchor &&
        g_object_get_data(G_OBJECT(anchor), TRAYMD_HRULE_ANCHOR_DATA) != NULL) {
      GtkWidget *hr = g_object_get_data(G_OBJECT(anchor), HR_WIDGET_DATA_KEY);
      if (hr) {
        gtk_widget_set_size_request(hr, width, HR_WIDGET_HEIGHT_PX);
      }
    }
    gtk_text_iter_forward_char(&iter);
  }
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event,
                                  gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  GtkTextIter iter;
  gint bx, by;
  gchar *url = NULL;
  GError *error = NULL;

  if (event->button != 1) {
    return FALSE;
  }

  /* Use Ctrl+Click to avoid opening links while selecting text. */
  if ((event->state & GDK_CONTROL_MASK) == 0) {
    return FALSE;
  }

  gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget),
                                        GTK_TEXT_WINDOW_TEXT, (gint)event->x,
                                        (gint)event->y, &bx, &by);
  gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, bx, by);

  if (!get_link_url_at_iter(self->buffer, &iter, &url)) {
    return FALSE;
  }

  if (g_uri_parse_scheme(url) == NULL) {
    gchar *with_scheme = g_strdup_printf("https://%s", url);
    g_free(url);
    url = with_scheme;
  }

  GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
  if (!gtk_show_uri_on_window(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel)
                                                      : NULL,
                              url, GDK_CURRENT_TIME, &error)) {
    if (error) {
      g_printerr("Failed to open link '%s': %s\n", url, error->message);
      g_clear_error(&error);
    }
  }

  g_free(url);
  return TRUE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event,
                                gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  GtkTextBuffer *buffer = self->buffer;
  GtkTextIter iter;
  gint bx, by;

  /* Middle click paste (PRIMARY selection) with undo-paste support. */
  if (event->button != 2) {
    return FALSE;
  }
  if (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK)) {
    return FALSE;
  }

  GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
  gchar *clip = gtk_clipboard_wait_for_text(cb);
  if (!clip) {
    return FALSE; /* fall back to default behavior */
  }

  gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget),
                                        GTK_TEXT_WINDOW_TEXT, (gint)event->x,
                                        (gint)event->y, &bx, &by);
  gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, bx, by);

  self->in_paste = TRUE;
  clear_last_paste(self);

  /* Middle-click paste typically inserts at pointer; don't replace selection. */
  self->paste_had_selection = FALSE;
  self->paste_replaced_text = g_strdup("");

  gtk_text_buffer_place_cursor(buffer, &iter);
  self->paste_sel_start_offset = gtk_text_iter_get_offset(&iter);
  self->paste_sel_end_offset = self->paste_sel_start_offset;

  self->paste_inserted_start =
      gtk_text_buffer_create_mark(buffer, NULL, &iter, TRUE);

  gtk_text_buffer_insert(buffer, &iter, clip, -1);
  self->paste_inserted_end =
      gtk_text_buffer_create_mark(buffer, NULL, &iter, FALSE);

  gtk_text_buffer_place_cursor(buffer, &iter);

  g_free(self->paste_clipboard_text);
  self->paste_clipboard_text = clip;
  self->paste_valid = TRUE;
  self->in_paste = FALSE;

  schedule_markdown_apply(self);
  return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event,
                                 gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  GtkTextIter iter;
  gint bx, by;
  gchar *url = NULL;

  gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget),
                                        GTK_TEXT_WINDOW_TEXT, (gint)event->x,
                                        (gint)event->y, &bx, &by);
  gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, bx, by);

  gboolean over_link = get_link_url_at_iter(self->buffer, &iter, &url);
  g_free(url);
  set_link_cursor(self, over_link);

  return FALSE;
}

static gboolean on_leave_notify(GtkWidget *widget, GdkEventCrossing *event,
                                gpointer user_data) {
  MarkydEditor *self = (MarkydEditor *)user_data;
  (void)widget;
  (void)event;
  set_link_cursor(self, FALSE);
  return FALSE;
}
