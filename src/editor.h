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
} MarkydEditor;

/* Lifecycle */
MarkydEditor *markyd_editor_new(MarkydApp *app);
void markyd_editor_free(MarkydEditor *editor);

/* Content management */
void markyd_editor_set_content(MarkydEditor *editor, const gchar *content);
gchar *markyd_editor_get_content(MarkydEditor *editor);

/* Widget access */
GtkWidget *markyd_editor_get_widget(MarkydEditor *editor);

#endif /* MARKYD_EDITOR_H */
