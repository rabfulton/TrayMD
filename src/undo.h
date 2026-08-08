#ifndef MARKYD_UNDO_H
#define MARKYD_UNDO_H

#include <glib.h>

typedef struct _MarkydUndo MarkydUndo;

MarkydUndo *markyd_undo_new(void);
void markyd_undo_free(MarkydUndo *undo);

/* Drop the whole history (note switch, content reload). */
void markyd_undo_clear(MarkydUndo *undo);

/*
 * Recording an edit takes two calls. markyd_undo_needs_step() updates the
 * grouping state and answers whether the edit has to open a new undo step;
 * only then does the caller snapshot the note and hand it to
 * markyd_undo_push(). Keeping them apart means a typed word costs one
 * snapshot instead of one per keystroke.
 *
 * Both are called before the edit reaches the buffer, so the snapshot is the
 * state Ctrl+Z has to come back to. Offsets are character offsets into the
 * buffer and only steer the grouping. "whitespace" marks an edit of a single
 * space or newline, which ends the step it belongs to, so that typing a
 * sentence undoes word by word.
 */
gboolean markyd_undo_needs_step(MarkydUndo *undo, gboolean insert, gint start,
                                gint end, gboolean whitespace);
void markyd_undo_push(MarkydUndo *undo, const gchar *markdown, gint cursor);

/*
 * Hand over the current state and get the neighbouring one back. Both return
 * FALSE when the matching stack is empty, leaving the out parameters
 * untouched. On success *out_markdown is owned by the caller.
 */
gboolean markyd_undo_undo(MarkydUndo *undo, const gchar *markdown, gint cursor,
                          gchar **out_markdown, gint *out_cursor);
gboolean markyd_undo_redo(MarkydUndo *undo, const gchar *markdown, gint cursor,
                          gchar **out_markdown, gint *out_cursor);

#endif /* MARKYD_UNDO_H */
