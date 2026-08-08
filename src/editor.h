#ifndef MARKYD_EDITOR_H
#define MARKYD_EDITOR_H

#include "undo.h"
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

  /* Undo/redo history */
  MarkydUndo *undo;

  /* Set while we rewrite the buffer ourselves (rendering, content load,
   * undo/redo) so those edits don't end up in the history. */
  gboolean undo_suppress;

  /* Depth of the GTK user action being applied, and whether it already
   * claimed its undo step. */
  gint user_action_depth;
  gboolean user_action_recorded;
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
