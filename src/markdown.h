#ifndef MARKYD_MARKDOWN_H
#define MARKYD_MARKDOWN_H

#include <gtk/gtk.h>

/* GObject data key used to mark hrule child anchors inserted into the buffer. */
#define TRAYMD_HRULE_ANCHOR_DATA "traymd-hr-anchor"
#define TRAYMD_TASK_ANCHOR_DATA "traymd-task-anchor"

typedef struct {
  gint marker_offset;  /* Character offset of '[' within the line. */
  gint content_offset; /* Character offset after the task prefix. */
  gboolean checked;
} MarkdownTask;

/* Recognize an optionally indented unordered task item, including display bullets. */
gboolean markdown_parse_task(const gchar *line, MarkdownTask *task);

/* Initialize markdown tags on a text buffer */
void markdown_init_tags(GtkTextBuffer *buffer);

/* Update accent colors for existing tags (after config changes). */
void markdown_update_accent_tags(GtkTextBuffer *buffer);

/* Apply markdown formatting to entire buffer */
void markdown_apply_tags(GtkTextBuffer *buffer);

#endif /* MARKYD_MARKDOWN_H */
