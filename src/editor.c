#include "editor.h"
#include "app.h"
#include "markdown.h"
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
static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event,
                                 gpointer user_data);
static gboolean on_leave_notify(GtkWidget *widget, GdkEventCrossing *event,
                                gpointer user_data);
static void apply_markdown(MarkydEditor *self);
static void schedule_markdown_apply(MarkydEditor *self);

static const gunichar UNORDERED_LIST_BULLET = 0x2022; /* '•' */

static gint compare_int_desc(gconstpointer a, gconstpointer b) {
  const gint ia = *(const gint *)a;
  const gint ib = *(const gint *)b;
  return (ib - ia);
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

  return g_string_free(out, FALSE);
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

  return g_string_free(out, FALSE);
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

static void normalize_list_markers(MarkydEditor *self) {
  GtkTextIter line_start, end;
  GArray *offsets = g_array_new(FALSE, FALSE, sizeof(gint));

  gtk_text_buffer_get_bounds(self->buffer, &line_start, &end);
  gtk_text_iter_set_line_offset(&line_start, 0);

  while (!gtk_text_iter_is_end(&line_start)) {
    gint offset = gtk_text_iter_get_offset(&line_start);
    GtkTextIter a = line_start;
    GtkTextIter b = line_start;

    if (gtk_text_iter_forward_char(&b)) {
      gunichar ch0 = gtk_text_iter_get_char(&a);
      gunichar ch1 = gtk_text_iter_get_char(&b);

      if ((ch0 == '-' || ch0 == '*') && ch1 == ' ') {
        g_array_append_val(offsets, offset);
      }
    }

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

  if (active) {
    GdkDisplay *display = gdk_window_get_display(win);
    GdkCursor *cursor = gdk_cursor_new_from_name(display, "pointer");
    if (!cursor) {
      cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
    }
    gdk_window_set_cursor(win, cursor);
    if (cursor) {
      g_object_unref(cursor);
    }
  } else {
    gdk_window_set_cursor(win, NULL);
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

  self->updating_tags = TRUE;
  normalize_list_markers(self);
  markdown_apply_tags(self->buffer);
  render_hrules(self);
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

MarkydEditor *markyd_editor_new(MarkydApp *app) {
  MarkydEditor *self = g_new0(MarkydEditor, 1);

  self->app = app;
  self->updating_tags = FALSE;
  self->markdown_idle_id = 0;

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

  g_signal_connect(self->text_view, "size-allocate",
                   G_CALLBACK(on_text_view_size_allocate), self);

  /* Link hover/click */
  gtk_widget_add_events(self->text_view, GDK_POINTER_MOTION_MASK |
                                            GDK_LEAVE_NOTIFY_MASK |
                                            GDK_BUTTON_RELEASE_MASK);
  g_signal_connect(self->text_view, "button-release-event",
                   G_CALLBACK(on_button_release), self);
  g_signal_connect(self->text_view, "motion-notify-event",
                   G_CALLBACK(on_motion_notify), self);
  g_signal_connect(self->text_view, "leave-notify-event",
                   G_CALLBACK(on_leave_notify), self);

  return self;
}

void markyd_editor_free(MarkydEditor *self) {
  if (!self)
    return;
  if (self->markdown_idle_id != 0) {
    g_source_remove(self->markdown_idle_id);
    self->markdown_idle_id = 0;
  }
  g_free(self);
}

void markyd_editor_set_content(MarkydEditor *self, const gchar *content) {
  gchar *display = markdown_to_display_text(content);
  self->updating_tags = TRUE;
  gtk_text_buffer_set_text(self->buffer, display ? display : "", -1);
  self->updating_tags = FALSE;
  g_free(display);

  /* Apply markdown formatting */
  schedule_markdown_apply(self);
}

gchar *markyd_editor_get_content(MarkydEditor *self) {
  GtkTextIter start, end;
  GString *out;
  GtkTextIter iter;
  gchar *raw;
  gchar *converted;

  gtk_text_buffer_get_bounds(self->buffer, &start, &end);

  /*
   * TRUE = include hidden chars (markdown syntax) so they're preserved when
   * saving, but skip embedded widget anchors (e.g., horizontal rules).
   */
  out = g_string_new(NULL);
  iter = start;
  while (!gtk_text_iter_equal(&iter, &end)) {
    GtkTextIter next = iter;
    gtk_text_iter_forward_char(&next);

    if (gtk_text_iter_get_child_anchor(&iter)) {
      iter = next;
      continue;
    }

    gchar *chunk = gtk_text_buffer_get_text(self->buffer, &iter, &next, TRUE);
    g_string_append(out, chunk);
    g_free(chunk);
    iter = next;
  }

  raw = g_string_free(out, FALSE);
  converted = display_to_markdown_text(raw);
  g_free(raw);
  return converted;
}

GtkWidget *markyd_editor_get_widget(MarkydEditor *self) {
  return self->text_view;
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

  /* Schedule auto-save */
  markyd_app_schedule_save(self->app);

  /* Apply markdown tags (deferred to avoid invalidating GTK iterators). */
  schedule_markdown_apply(self);
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
