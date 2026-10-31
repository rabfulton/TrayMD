#include "app.h"
#include "config.h"
#include "editor.h"
#include "markdown.h"
#include "window.h"

/* Exercise the editor without writing notes or changing the real application. */
void markyd_app_schedule_save(MarkydApp *app) { app->modified = TRUE; }
void markyd_app_new_note(MarkydApp *app) { (void)app; g_assert_not_reached(); }
void markyd_app_next_note(MarkydApp *app) { (void)app; g_assert_not_reached(); }
void markyd_app_prev_note(MarkydApp *app) { (void)app; g_assert_not_reached(); }
gboolean markyd_app_delete_current_note(MarkydApp *app) {
  (void)app;
  g_assert_not_reached();
  return FALSE;
}
gint markyd_app_get_note_count(MarkydApp *app) { (void)app; return 0; }

static void flush_events(void) {
  while (g_main_context_iteration(NULL, FALSE)) {}
}

static MarkydEditor *new_editor(MarkydApp *app, const gchar *content) {
  markyd_window_apply_css(NULL);
  MarkydEditor *editor = markyd_editor_new(app);
  g_object_ref_sink(editor->text_view);
  GtkWidget *window = gtk_offscreen_window_new();
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scroll), editor->text_view);
  gtk_container_add(GTK_CONTAINER(window), scroll);
  gtk_widget_set_size_request(window, 600, 300);
  gtk_widget_show_all(window);
  g_object_set_data(G_OBJECT(editor->text_view), "test-window", window);
  markyd_editor_set_content(editor, content);
  flush_events();
  return editor;
}

static void free_editor(MarkydEditor *editor) {
  GtkWidget *view = editor->text_view;
  GtkWidget *window = g_object_get_data(G_OBJECT(view), "test-window");
  markyd_editor_free(editor);
  gtk_widget_destroy(window);
  g_object_unref(view);
}

static void assert_content(MarkydEditor *editor, const gchar *expected) {
  gchar *actual = markyd_editor_get_content(editor);
  g_assert_cmpstr(actual, ==, expected);
  g_free(actual);
}

static GtkWidget *checkbox_at_line(MarkydEditor *editor, gint line) {
  GtkTextIter iter;
  gtk_text_buffer_get_iter_at_line(editor->buffer, &iter, line);
  do {
    GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&iter);
    if (anchor && g_object_get_data(G_OBJECT(anchor), TRAYMD_TASK_ANCHOR_DATA)) {
      GList *widgets = gtk_text_child_anchor_get_widgets(anchor);
      GtkWidget *check = widgets ? widgets->data : NULL;
      g_list_free(widgets);
      g_assert_true(GTK_IS_CHECK_BUTTON(check));
      GtkTextTag *invisible = gtk_text_tag_table_lookup(
          gtk_text_buffer_get_tag_table(editor->buffer), "invisible");
      g_assert_false(gtk_text_iter_has_tag(&iter, invisible));
      return check;
    }
  } while (gtk_text_iter_forward_char(&iter) && !gtk_text_iter_starts_line(&iter));
  return NULL;
}

static void assert_tag_at(MarkydEditor *editor, const gchar *text,
                          const gchar *tag_name, gboolean expected) {
  GtkTextIter start, match, end;
  gtk_text_buffer_get_start_iter(editor->buffer, &start);
  g_assert_true(gtk_text_iter_forward_search(&start, text, GTK_TEXT_SEARCH_TEXT_ONLY,
                                             &match, &end, NULL));
  while (gtk_text_iter_get_child_anchor(&match)) {
    gtk_text_iter_forward_char(&match);
  }
  GtkTextTag *tag = gtk_text_tag_table_lookup(
      gtk_text_buffer_get_tag_table(editor->buffer), tag_name);
  g_assert_cmpint(gtk_text_iter_has_tag(&match, tag), ==, expected);
}

static gboolean press_enter(MarkydEditor *editor) {
  gboolean handled = FALSE;
  GdkEventKey event = {0};
  GtkTextIter end;
  event.type = GDK_KEY_PRESS;
  event.keyval = GDK_KEY_Return;
  event.window = gtk_text_view_get_window(GTK_TEXT_VIEW(editor->text_view),
                                          GTK_TEXT_WINDOW_TEXT);
  gtk_text_buffer_get_end_iter(editor->buffer, &end);
  gtk_text_buffer_place_cursor(editor->buffer, &end);
  g_signal_emit_by_name(editor->text_view, "key-press-event", &event, &handled);
  flush_events();
  return handled;
}

static void test_parse(void) {
  const gchar *valid[] = {"- [ ] task", "* [x] task", "+ [X] task",
                          "  - [ ] nested", "\t+\t[x]\ttask", "• [ ] café",
                          "- [ ]", "- [x] ", NULL};
  const gchar *invalid[] = {"", "-", "- [", "- [ ", "- [x", "- [y] task",
                            "- [xx] task", "- [x]text", "[ ] task",
                            "text - [ ] task", "- ordinary", NULL};
  for (guint i = 0; valid[i]; i++) {
    MarkdownTask task;
    g_assert_true(markdown_parse_task(valid[i], &task));
    const gchar *marker = g_utf8_offset_to_pointer(valid[i], task.marker_offset);
    g_assert_cmpint(*marker, ==, '[');
    g_assert_cmpint(task.checked, ==, marker[1] != ' ');
  }
  for (guint i = 0; invalid[i]; i++) {
    g_assert_false(markdown_parse_task(invalid[i], NULL));
  }
  g_assert_false(markdown_parse_task(NULL, NULL));
}

static void test_render_and_toggle(void) {
  const gchar *content = "- [ ] Buy **café**\n* [X] Finished\n  + [x] Nested\n";
  MarkydApp app = {0};
  MarkydEditor *editor = new_editor(&app, content);
  assert_content(editor, content);
  g_assert_false(app.modified);
  g_assert_false(gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(checkbox_at_line(editor, 0))));
  g_assert_true(gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(checkbox_at_line(editor, 1))));
  g_assert_nonnull(checkbox_at_line(editor, 2));
  assert_tag_at(editor, "Buy", "task_done", FALSE);
  assert_tag_at(editor, "- [ ]", "invisible", TRUE);
  assert_tag_at(editor, "Finished", "task_done", TRUE);
  assert_tag_at(editor, "café", "bold", TRUE);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox_at_line(editor, 0)), TRUE);
  flush_events();
  g_assert_true(app.modified);
  assert_content(editor, "- [x] Buy **café**\n* [X] Finished\n  + [x] Nested\n");
  assert_tag_at(editor, "Buy", "task_done", TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox_at_line(editor, 1)), FALSE);
  flush_events();
  assert_content(editor, "- [x] Buy **café**\n* [ ] Finished\n  + [x] Nested\n");
  assert_tag_at(editor, "Finished", "task_done", FALSE);

  /* Repeated rendering must preserve source, states, and one checkbox per task. */
  for (gint i = 0; i < 3; i++) {
    markyd_editor_refresh(editor);
    flush_events();
  }
  gchar *saved = markyd_editor_get_content(editor);
  markyd_editor_set_content(editor, saved);
  flush_events();
  assert_content(editor, saved);
  g_free(saved);
  g_assert_nonnull(checkbox_at_line(editor, 0));
  g_assert_nonnull(checkbox_at_line(editor, 1));
  free_editor(editor);
}

static void test_enter(void) {
  const gchar *tasks[] = {"- [x] Done", "  * [X] Nested", "+ [ ] Todo", NULL};
  const gchar *continued[] = {"- [x] Done\n- [ ] ",
                              "  * [X] Nested\n  * [ ] ",
                              "+ [ ] Todo\n+ [ ] ", NULL};
  const gchar *ended[] = {"- [x] Done\n", "  * [X] Nested\n", "+ [ ] Todo\n"};
  for (guint i = 0; tasks[i]; i++) {
    MarkydApp app = {0};
    MarkydEditor *editor = new_editor(&app, tasks[i]);
    g_assert_true(press_enter(editor));
    g_assert_true(app.modified);
    assert_content(editor, continued[i]);
    app.modified = FALSE;
    g_assert_true(press_enter(editor));
    g_assert_true(app.modified);
    assert_content(editor, ended[i]);
    free_editor(editor);
  }

  MarkydApp app = {0};
  MarkydEditor *editor = new_editor(&app, "- [X]");
  g_assert_true(press_enter(editor));
  assert_content(editor, "");
  free_editor(editor);
}

static void test_literal_and_hrules(void) {
  const gchar *content = "- [ ] First\n---\n```\n* [x] literal\n```\n"
                         "  - [X] Last\n- [y] invalid\n`- [ ] inline`\n";
  MarkydApp app = {0};
  MarkydEditor *editor = new_editor(&app, content);
  assert_content(editor, content);
  g_assert_nonnull(checkbox_at_line(editor, 0));
  g_assert_null(checkbox_at_line(editor, 3));
  g_assert_nonnull(checkbox_at_line(editor, 5));
  g_assert_null(checkbox_at_line(editor, 6));
  g_assert_null(checkbox_at_line(editor, 7));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox_at_line(editor, 5)), FALSE);
  flush_events();
  assert_content(editor, "- [ ] First\n---\n```\n* [x] literal\n```\n"
                         "  - [ ] Last\n- [y] invalid\n`- [ ] inline`\n");
  free_editor(editor);

  editor = new_editor(&app, "```\n- [ ] literal");
  /* The default text view handles Return inside fenced code. */
  press_enter(editor);
  assert_content(editor, "```\n- [ ] literal\n");
  free_editor(editor);
}

static void test_live_editing(void) {
  MarkydApp app = {0};
  MarkydEditor *editor = new_editor(&app, "");
  gtk_text_buffer_insert_at_cursor(editor->buffer, "- [ ] New task", -1);
  flush_events();
  g_assert_true(app.modified);
  g_assert_nonnull(checkbox_at_line(editor, 0));
  assert_content(editor, "- [ ] New task");

  /* Moving a rendered task into a code block must remove the checkbox. */
  GtkTextIter start;
  gtk_text_buffer_get_start_iter(editor->buffer, &start);
  gtk_text_buffer_insert(editor->buffer, &start, "```\n", -1);
  flush_events();
  g_assert_null(checkbox_at_line(editor, 1));
  assert_content(editor, "```\n- [ ] New task");

  /* Replacing a task with a regular bullet restores ordinary list behavior. */
  gtk_text_buffer_set_text(editor->buffer, "- ordinary", -1);
  flush_events();
  g_assert_null(checkbox_at_line(editor, 0));
  g_assert_true(press_enter(editor));
  assert_content(editor, "- ordinary\n- ");
  free_editor(editor);

  /* Deleting the final task's source must also remove its now-stale anchor. */
  editor = new_editor(&app, "- [ ] Delete me");
  GtkTextIter end;
  gtk_text_buffer_get_start_iter(editor->buffer, &start);
  gtk_text_iter_forward_char(&start);
  gtk_text_buffer_get_end_iter(editor->buffer, &end);
  gtk_text_buffer_delete(editor->buffer, &start, &end);
  flush_events();
  g_assert_cmpint(gtk_text_buffer_get_char_count(editor->buffer), ==, 0);
  assert_content(editor, "");
  free_editor(editor);
}

static void wait_for_layout(MarkydEditor *editor) {
  gtk_widget_queue_draw(editor->text_view);
  gtk_test_widget_wait_for_draw(editor->text_view);
  flush_events();
}

static void assert_checkbox_pixels(MarkydEditor *editor, GtkWidget *check) {
  GtkWidget *window = g_object_get_data(G_OBJECT(editor->text_view), "test-window");
  GdkPixbuf *pixbuf = gtk_offscreen_window_get_pixbuf(GTK_OFFSCREEN_WINDOW(window));
  g_assert_nonnull(pixbuf);
  gint x, y;
  g_assert_true(gtk_widget_translate_coordinates(check, window, 0, 0, &x, &y));
  gint width = gtk_widget_get_allocated_width(check);
  gint height = gtk_widget_get_allocated_height(check);
  gint stride = gdk_pixbuf_get_rowstride(pixbuf);
  gint channels = gdk_pixbuf_get_n_channels(pixbuf);
  guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
  gint bg_x = gdk_pixbuf_get_width(pixbuf) - 2;
  gint bg_y = gdk_pixbuf_get_height(pixbuf) - 2;
  const guchar *background = pixels + bg_y * stride + bg_x * channels;
  gint painted = 0;
  g_assert_cmpint(x, >=, 0);
  g_assert_cmpint(y, >=, 0);
  g_assert_cmpint(x + width, <=, gdk_pixbuf_get_width(pixbuf));
  g_assert_cmpint(y + height, <=, gdk_pixbuf_get_height(pixbuf));
  for (gint row = y; row < y + height; row++) {
    for (gint col = x; col < x + width; col++) {
      const guchar *pixel = pixels + row * stride + col * channels;
      if (ABS(pixel[0] - background[0]) > 32 ||
          ABS(pixel[1] - background[1]) > 32 ||
          ABS(pixel[2] - background[2]) > 32) {
        painted++;
      }
    }
  }
  g_assert_cmpint(painted, >, width);
  g_object_unref(pixbuf);
}

static void test_checkbox_layout_and_themes(void) {
  MarkydApp app = {0};
  MarkydEditor *editor = new_editor(&app, "- [ ] First\n- [x] Second\n");
  GtkWidget *check = checkbox_at_line(editor, 0);
  const gchar *themes[] = {"dark", "light", "system"};
  const gint font_sizes[] = {10, 16, 24};

  for (guint theme = 0; theme < G_N_ELEMENTS(themes); theme++) {
    g_free(config->theme);
    config->theme = g_strdup(themes[theme]);
    gint previous_size = 0;
    for (guint font = 0; font < G_N_ELEMENTS(font_sizes); font++) {
      config->font_size = font_sizes[font];
      markyd_window_apply_css(NULL);
      markyd_editor_refresh(editor);
      flush_events();
      wait_for_layout(editor);
      g_assert_true(checkbox_at_line(editor, 0) == check);
      g_assert_true(gtk_widget_get_mapped(check));
      GtkAllocation allocation;
      gtk_widget_get_allocation(check, &allocation);
      g_assert_cmpint(allocation.height, >, previous_size);
      previous_size = allocation.height;

      GtkTextIter anchor;
      GdkRectangle rect;
      gint x, y;
      gtk_text_buffer_get_start_iter(editor->buffer, &anchor);
      gtk_text_view_get_iter_location(GTK_TEXT_VIEW(editor->text_view), &anchor,
                                       &rect);
      gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(editor->text_view),
          GTK_TEXT_WINDOW_WIDGET, rect.x, rect.y, &x, &y);
      g_assert_cmpint(allocation.x, ==, x);
      g_assert_cmpint(allocation.y, >=, y);
      g_assert_cmpint(allocation.y + allocation.height, <=, y + rect.height);

      /* The Markdown separator is a visible space between the box and text. */
      GtkTextIter content, match_end;
      g_assert_true(gtk_text_iter_forward_search(&anchor, "First", 0,
                                                 &content, &match_end, NULL));
      GdkRectangle content_rect;
      gtk_text_view_get_iter_location(GTK_TEXT_VIEW(editor->text_view), &content,
                                       &content_rect);
      PangoLayout *space = gtk_widget_create_pango_layout(editor->text_view, " ");
      gint space_width;
      pango_layout_get_pixel_size(space, &space_width, NULL);
      g_object_unref(space);
      g_assert_cmpint(content_rect.x - rect.x - rect.width, >=, space_width);

      GdkRGBA accent, actual;
      g_assert_true(gdk_rgba_parse(&accent, config->list_bullet_color));
      gtk_style_context_get_color(gtk_widget_get_style_context(check),
                                   GTK_STATE_FLAG_NORMAL, &actual);
      g_assert_true(gdk_rgba_equal(&accent, &actual));
      assert_checkbox_pixels(editor, check);

      for (gint click = 0; click < 2; click++) {
        gtk_button_clicked(GTK_BUTTON(check));
        flush_events();
        wait_for_layout(editor);
        g_assert_true(checkbox_at_line(editor, 0) == check);
        g_assert_true(gtk_widget_get_mapped(check));
        g_assert_cmpint(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)),
                        ==, click == 0);
        GtkAllocation after;
        gtk_widget_get_allocation(check, &after);
        g_assert_cmpint(after.x, ==, allocation.x);
        g_assert_cmpint(after.y, ==, allocation.y);
        g_assert_cmpint(after.width, ==, allocation.width);
        g_assert_cmpint(after.height, ==, allocation.height);
        assert_checkbox_pixels(editor, check);
      }
    }
  }
  assert_content(editor, "- [ ] First\n- [x] Second\n");
  free_editor(editor);
}

static void test_typing_bottom_space(void) {
  GString *note = g_string_new(NULL);
  for (gint line = 0; line < 50; line++) {
    g_string_append(note, "A line of notes\n");
  }

  const gint font_sizes[] = {10, 24};
  for (guint font = 0; font < G_N_ELEMENTS(font_sizes); font++) {
    config->font_size = font_sizes[font];
    MarkydApp app = {0};
    MarkydEditor *editor = new_editor(&app, note->str);
    GtkWidget *window = g_object_get_data(G_OBJECT(editor->text_view), "test-window");
    gtk_widget_grab_focus(editor->text_view);
    GtkTextIter cursor;
    gtk_text_buffer_get_end_iter(editor->buffer, &cursor);
    gtk_text_buffer_place_cursor(editor->buffer, &cursor);

    for (gint line = 0; line < 3; line++) {
      /* Exercise GTK's editing action and native scrolling to the caret. */
      g_signal_emit_by_name(editor->text_view, "insert-at-cursor", "Typing\n");
      flush_events();
      gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(editor->text_view),
                                         gtk_text_buffer_get_insert(editor->buffer));
      wait_for_layout(editor);
      gtk_text_buffer_get_iter_at_mark(editor->buffer, &cursor,
                                       gtk_text_buffer_get_insert(editor->buffer));
      GdkRectangle caret;
      gtk_text_view_get_iter_location(GTK_TEXT_VIEW(editor->text_view), &cursor,
                                       &caret);
      gint x, y, view_x, view_y;
      gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(editor->text_view),
          GTK_TEXT_WINDOW_WIDGET, caret.x, caret.y, &x, &y);
      g_assert_true(gtk_widget_translate_coordinates(editor->text_view, window,
                                                     0, 0, &view_x, &view_y));
      gint gap = gtk_widget_get_allocated_height(window) - view_y - y - caret.height;
      g_assert_cmpint(y, >=, 0);
      g_assert_cmpint(gap, >=, caret.height);
    }

    gchar *expected = g_strconcat(note->str, "Typing\nTyping\nTyping\n", NULL);
    assert_content(editor, expected);
    g_free(expected);
    free_editor(editor);
  }
  g_string_free(note, TRUE);
}

int main(int argc, char **argv) {
  gtk_test_init(&argc, &argv, NULL);
  config = config_new();
  g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, NULL);
  g_test_add_func("/tasks/parse", test_parse);
  g_test_add_func("/tasks/render-toggle-roundtrip", test_render_and_toggle);
  g_test_add_func("/tasks/enter", test_enter);
  g_test_add_func("/tasks/code-and-hrules", test_literal_and_hrules);
  g_test_add_func("/tasks/live-editing", test_live_editing);
  g_test_add_func("/tasks/checkbox-layout-and-themes", test_checkbox_layout_and_themes);
  g_test_add_func("/editor/typing-bottom-space", test_typing_bottom_space);
  gint result = g_test_run();
  config_free(config);
  return result;
}
