#include "app.h"
#include "config.h"
#include "editor.h"
#include "window.h"

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

static void send_key(MarkydEditor *editor, guint keyval, GdkModifierType state) {
  GdkEventKey event = {0};
  gboolean handled = FALSE;

  event.keyval = keyval;
  event.state = state;
  g_signal_emit_by_name(editor->text_view, "key-press-event", &event, &handled);
  g_assert_true(handled);
  flush_events();
}

static void undo(MarkydEditor *editor) { send_key(editor, GDK_KEY_z, GDK_CONTROL_MASK); }

static void redo(MarkydEditor *editor) { send_key(editor, GDK_KEY_y, GDK_CONTROL_MASK); }

static void move_to_end(MarkydEditor *editor) {
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(editor->buffer, &end);
  gtk_text_buffer_place_cursor(editor->buffer, &end);
}

static void type_text(MarkydEditor *editor, const gchar *text) {
  for (const gchar *p = text; *p; p = g_utf8_next_char(p)) {
    gtk_text_buffer_insert_interactive_at_cursor(
        editor->buffer, p, g_utf8_next_char(p) - p, TRUE);
  }
  flush_events();
}

static void paste(MarkydEditor *editor, const gchar *text) {
  GtkClipboard *clipboard =
      gtk_widget_get_clipboard(editor->text_view, GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_text(clipboard, text, -1);
  g_signal_emit_by_name(editor->text_view, "paste-clipboard");
  flush_events();
}

static void test_restore_rendered_list_marker(void) {
  MarkydApp app = {0};
  MarkydEditor *editor = new_editor(&app, "---\nhi");
  GtkTextIter start, end;

  gtk_text_buffer_get_bounds(editor->buffer, &start, &end);
  gtk_text_buffer_select_range(editor->buffer, &start, &end);
  gtk_text_buffer_begin_user_action(editor->buffer);
  gtk_text_buffer_delete_selection(editor->buffer, TRUE, TRUE);
  gtk_text_buffer_insert_at_cursor(editor->buffer, "- a", -1);
  gtk_text_buffer_end_user_action(editor->buffer);
  flush_events();
  undo(editor);
  assert_content(editor, "---\nhi");

  free_editor(editor);
}

static void test_restore_keeps_code_literal(void) {
  MarkydApp app = {0};
  MarkydEditor *editor = new_editor(&app, "```\n* literal\n```");

  move_to_end(editor);
  type_text(editor, "\nmore");
  assert_content(editor, "```\n* literal\n```\nmore");
  undo(editor);
  undo(editor);
  assert_content(editor, "```\n* literal\n```");
  redo(editor);
  redo(editor);
  assert_content(editor, "```\n* literal\n```\nmore");

  free_editor(editor);
}

static void test_paste_opens_undo_step(void) {
  MarkydApp app = {0};
  MarkydEditor *editor = new_editor(&app, "");

  type_text(editor, "abc");
  paste(editor, "PASTED");
  assert_content(editor, "abcPASTED");
  undo(editor);
  assert_content(editor, "abc");

  free_editor(editor);
}

static void test_single_char_paste_opens_undo_step(void) {
  MarkydApp app = {0};
  MarkydEditor *editor = new_editor(&app, "");

  type_text(editor, "abc");
  paste(editor, "X");
  assert_content(editor, "abcX");
  undo(editor);
  assert_content(editor, "abc");

  free_editor(editor);
}

static void test_selection_replace_opens_undo_step(void) {
  MarkydApp app = {0};
  MarkydEditor *editor = new_editor(&app, "abc");
  GtkTextIter start, end;

  move_to_end(editor);
  gtk_text_buffer_get_end_iter(editor->buffer, &end);
  gtk_text_buffer_backspace(editor->buffer, &end, TRUE, TRUE);
  assert_content(editor, "ab");
  gtk_text_buffer_get_iter_at_offset(editor->buffer, &start, 1);
  gtk_text_buffer_get_iter_at_offset(editor->buffer, &end, 2);
  gtk_text_buffer_select_range(editor->buffer, &start, &end);
  paste(editor, "PASTED");
  assert_content(editor, "aPASTED");
  undo(editor);
  assert_content(editor, "ab");

  free_editor(editor);
}

static void test_newline_opens_undo_step(void) {
  MarkydApp app = {0};
  MarkydEditor *editor = new_editor(&app, "- abc");

  move_to_end(editor);
  send_key(editor, GDK_KEY_Return, 0);
  undo(editor);
  assert_content(editor, "- abc");

  free_editor(editor);
}

int main(int argc, char **argv) {
  gtk_test_init(&argc, &argv, NULL);
  config = config_new();
  g_test_add_func("/undo/restore-rendered-list-marker",
                  test_restore_rendered_list_marker);
  g_test_add_func("/undo/restore-keeps-code-literal",
                  test_restore_keeps_code_literal);
  g_test_add_func("/undo/paste-opens-step", test_paste_opens_undo_step);
  g_test_add_func("/undo/single-char-paste-opens-step",
                  test_single_char_paste_opens_undo_step);
  g_test_add_func("/undo/selection-replace-opens-step",
                  test_selection_replace_opens_undo_step);
  g_test_add_func("/undo/newline-opens-step", test_newline_opens_undo_step);
  int result = g_test_run();
  config_free(config);
  return result;
}
