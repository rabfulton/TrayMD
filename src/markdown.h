#ifndef MARKYD_MARKDOWN_H
#define MARKYD_MARKDOWN_H

#include <gtk/gtk.h>

/* Initialize markdown tags on a text buffer */
void markdown_init_tags(GtkTextBuffer *buffer);

/* Apply markdown formatting to entire buffer */
void markdown_apply_tags(GtkTextBuffer *buffer);

#endif /* MARKYD_MARKDOWN_H */
