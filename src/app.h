#ifndef MARKYD_APP_H
#define MARKYD_APP_H

#include <gtk/gtk.h>

/* Forward declarations */
typedef struct _MarkydWindow MarkydWindow;
typedef struct _MarkydEditor MarkydEditor;

typedef enum _MarkydTrayBackend {
  MARKYD_TRAY_BACKEND_STATUSICON = 0,
  MARKYD_TRAY_BACKEND_APPINDICATOR = 1,
} MarkydTrayBackend;

/* Application state */
typedef struct _MarkydApp {
  GtkApplication *gtk_app;
  MarkydWindow *window;
  MarkydEditor *editor;

  /* Note management */
  GPtrArray *note_paths; /* Array of note file paths */
  gint current_index;    /* Current note index (-1 if none) */

  /* Auto-save */
  guint save_timeout_id; /* Pending save timeout */
  gboolean modified;     /* Current note has unsaved changes */

  /* Startup options */
  gboolean start_minimized; /* Start minimized to tray */
  MarkydTrayBackend tray_backend;
} MarkydApp;

/* Global app instance */
extern MarkydApp *app;

/* Lifecycle */
MarkydApp *markyd_app_new(void);
void markyd_app_free(MarkydApp *app);
int markyd_app_run(MarkydApp *app, int argc, char **argv);

/* Note navigation */
void markyd_app_refresh_notes(MarkydApp *app);
void markyd_app_goto_note(MarkydApp *app, gint index);
void markyd_app_next_note(MarkydApp *app);
void markyd_app_prev_note(MarkydApp *app);
void markyd_app_new_note(MarkydApp *app);

/* Auto-save */
void markyd_app_schedule_save(MarkydApp *app);
void markyd_app_save_current(MarkydApp *app);

/* Utility */
const gchar *markyd_app_get_current_path(MarkydApp *app);
gint markyd_app_get_note_count(MarkydApp *app);

#endif /* MARKYD_APP_H */
