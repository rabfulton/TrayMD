#include "undo.h"

/* Keeping whole snapshots is affordable for notes this size, and it makes an
 * undo land on a state the editor has already shown, never on one rebuilt from
 * offset arithmetic. */
#define UNDO_MAX_STEPS 100

/* A pause this long ends the current step, so a thought break is undoable. */
#define UNDO_PAUSE_US G_USEC_PER_SEC

typedef struct _UndoStep {
  gchar *markdown;
  gint cursor;
} UndoStep;

struct _MarkydUndo {
  GQueue *undo; /* head is the most recent step */
  GQueue *redo;

  /* State of the step still being filled in. */
  gboolean group_open;
  gboolean group_insert;
  gint group_edge; /* offset the next edit has to touch to be coalesced */
  gint64 group_time;
  gboolean break_after;
};

static UndoStep *step_new(const gchar *markdown, gint cursor) {
  UndoStep *step = g_new0(UndoStep, 1);

  step->markdown = g_strdup(markdown ? markdown : "");
  step->cursor = cursor;
  return step;
}

static void step_free(gpointer data) {
  UndoStep *step = (UndoStep *)data;

  if (!step) {
    return;
  }
  g_free(step->markdown);
  g_free(step);
}

static void close_group(MarkydUndo *self) {
  self->group_open = FALSE;
  self->group_insert = FALSE;
  self->group_edge = 0;
  self->group_time = 0;
  self->break_after = FALSE;
}

/* Hand the step's text to the caller and drop the wrapper. */
static void step_take(UndoStep *step, gchar **out_markdown, gint *out_cursor) {
  *out_markdown = step->markdown;
  *out_cursor = step->cursor;
  g_free(step);
}

MarkydUndo *markyd_undo_new(void) {
  MarkydUndo *self = g_new0(MarkydUndo, 1);

  self->undo = g_queue_new();
  self->redo = g_queue_new();
  close_group(self);

  return self;
}

void markyd_undo_free(MarkydUndo *self) {
  if (!self) {
    return;
  }

  g_queue_free_full(self->undo, step_free);
  g_queue_free_full(self->redo, step_free);
  g_free(self);
}

void markyd_undo_clear(MarkydUndo *self) {
  if (!self) {
    return;
  }

  g_queue_free_full(self->undo, step_free);
  g_queue_free_full(self->redo, step_free);
  self->undo = g_queue_new();
  self->redo = g_queue_new();
  close_group(self);
}

gboolean markyd_undo_needs_step(MarkydUndo *self, gboolean insert, gint start,
                                gint end, gboolean whitespace) {
  gint64 now;
  gboolean adjacent;
  gboolean opens;

  if (!self) {
    return FALSE;
  }

  now = g_get_monotonic_time();

  /*
   * Typing extends the step forwards, so the next insert has to start where
   * the last one ended. Deleting collapses towards its own start, which both
   * Backspace (range ends at the edge) and Delete (range starts at it) keep
   * touching.
   */
  adjacent = insert ? (start == self->group_edge)
                    : (start == self->group_edge || end == self->group_edge);

  opens = !self->group_open || self->break_after ||
          self->group_insert != insert || !adjacent ||
          (now - self->group_time) > UNDO_PAUSE_US;

  self->group_open = TRUE;
  self->group_insert = insert;
  self->group_edge = insert ? end : start;
  self->group_time = now;
  self->break_after = whitespace;

  return opens;
}

void markyd_undo_push(MarkydUndo *self, const gchar *markdown, gint cursor) {
  if (!self) {
    return;
  }

  /* A fresh edit makes anything that was undone unreachable. */
  g_queue_free_full(self->redo, step_free);
  self->redo = g_queue_new();

  g_queue_push_head(self->undo, step_new(markdown, cursor));

  while (g_queue_get_length(self->undo) > UNDO_MAX_STEPS) {
    step_free(g_queue_pop_tail(self->undo));
  }
}

gboolean markyd_undo_undo(MarkydUndo *self, const gchar *markdown, gint cursor,
                          gchar **out_markdown, gint *out_cursor) {
  UndoStep *step;

  if (!self || !out_markdown || !out_cursor || g_queue_is_empty(self->undo)) {
    return FALSE;
  }

  step = g_queue_pop_head(self->undo);
  g_queue_push_head(self->redo, step_new(markdown, cursor));
  step_take(step, out_markdown, out_cursor);

  /* Whatever gets typed next belongs to a step of its own. */
  close_group(self);

  return TRUE;
}

gboolean markyd_undo_redo(MarkydUndo *self, const gchar *markdown, gint cursor,
                          gchar **out_markdown, gint *out_cursor) {
  UndoStep *step;

  if (!self || !out_markdown || !out_cursor || g_queue_is_empty(self->redo)) {
    return FALSE;
  }

  step = g_queue_pop_head(self->redo);
  g_queue_push_head(self->undo, step_new(markdown, cursor));
  step_take(step, out_markdown, out_cursor);

  close_group(self);

  return TRUE;
}
