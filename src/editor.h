#ifndef MARKYD_EDITOR_H
#define MARKYD_EDITOR_H

#include <gtk/gtk.h>

typedef struct _MarkydApp MarkydApp;

typedef struct _MarkydEditor {
  GtkWidget *text_view;
  GtkTextBuffer *buffer;
  MarkydApp *app;

  /* Prevent recursive tag application */
  gboolean updating_tags;

  /* Coalesce markdown re-rendering to idle to avoid invalidating GTK iterators. */
  guint markdown_idle_id;

  /* "Undo last paste" support (single-level) */
  gboolean in_paste;
  gboolean in_undo;
  gboolean pending_paste_finalize;
  gint paste_start_offset;
  gint paste_end_offset_before;
  gchar *paste_replaced_text;
  gchar *paste_clipboard_text;
  GtkTextMark *paste_inserted_start;
  GtkTextMark *paste_inserted_end;
  gboolean paste_valid;
  gboolean paste_had_selection;
  gint paste_sel_start_offset;
  gint paste_sel_end_offset;
} MarkydEditor;

/* Lifecycle */
MarkydEditor *markyd_editor_new(MarkydApp *app);
void markyd_editor_free(MarkydEditor *editor);

/* Content management */
void markyd_editor_set_content(MarkydEditor *editor, const gchar *content);
gchar *markyd_editor_get_content(MarkydEditor *editor);

/* Widget access */
GtkWidget *markyd_editor_get_widget(MarkydEditor *editor);
void markyd_editor_focus(MarkydEditor *editor);

/* Force a refresh of markdown styling/rendering (e.g., after settings change). */
void markyd_editor_refresh(MarkydEditor *editor);

#endif /* MARKYD_EDITOR_H */
