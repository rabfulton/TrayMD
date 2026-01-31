#ifndef MARKYD_MARKDOWN_H
#define MARKYD_MARKDOWN_H

#include <gtk/gtk.h>

/* GObject data key used to mark hrule child anchors inserted into the buffer. */
#define TRAYMD_HRULE_ANCHOR_DATA "traymd-hr-anchor"

/* Initialize markdown tags on a text buffer */
void markdown_init_tags(GtkTextBuffer *buffer);

/* Update accent colors for existing tags (after config changes). */
void markdown_update_accent_tags(GtkTextBuffer *buffer);

/* Apply markdown formatting to entire buffer */
void markdown_apply_tags(GtkTextBuffer *buffer);

#endif /* MARKYD_MARKDOWN_H */
