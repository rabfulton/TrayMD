#include "app.h"
#include "config.h"
#include "editor.h"
#include "notes.h"
#include "tray.h"
#include "window.h"

/* Global app instance */
MarkydApp *app = NULL;

/* Auto-save delay in milliseconds */
#define AUTOSAVE_DELAY_MS 500

static void on_activate(GtkApplication *gtk_app, gpointer user_data);
static gboolean on_autosave_timeout(gpointer user_data);

MarkydApp *markyd_app_new(void) {
  MarkydApp *self = g_new0(MarkydApp, 1);

  /* Initialize and load config */
  config = config_new();
  config_load(config);

  GApplicationFlags flags =
#if GLIB_CHECK_VERSION(2, 74, 0)
      G_APPLICATION_DEFAULT_FLAGS;
#else
      G_APPLICATION_FLAGS_NONE;
#endif

  self->gtk_app = gtk_application_new("org.traymd.app", flags);
  self->note_paths = g_ptr_array_new_with_free_func(g_free);
  self->current_index = -1;
  self->save_timeout_id = 0;
  self->modified = FALSE;
  self->tray_backend = MARKYD_TRAY_BACKEND_STATUSICON;
  self->no_tray = FALSE;

  g_signal_connect(self->gtk_app, "activate", G_CALLBACK(on_activate), self);

  /* Set global instance */
  app = self;

  return self;
}

void markyd_app_free(MarkydApp *self) {
  if (!self)
    return;

  /* Cancel pending save */
  if (self->save_timeout_id > 0) {
    g_source_remove(self->save_timeout_id);
    /* Force save any pending changes */
    markyd_app_save_current(self);
  }

  tray_cleanup();

  if (self->window) {
    markyd_window_free(self->window);
  }

  if (self->note_paths) {
    g_ptr_array_free(self->note_paths, TRUE);
  }

  g_object_unref(self->gtk_app);

  /* Save and free config */
  config_save(config);
  config_free(config);
  config = NULL;

  g_free(self);

  app = NULL;
}

int markyd_app_run(MarkydApp *self, int argc, char **argv) {
  return g_application_run(G_APPLICATION(self->gtk_app), argc, argv);
}

static void on_activate(GtkApplication *gtk_app, gpointer user_data) {
  MarkydApp *self = (MarkydApp *)user_data;

  (void)gtk_app; /* Unused */

  /* GtkApplication "activate" can be emitted multiple times (e.g. when the user
   * launches the app again). Avoid re-initializing the tray/window/note state. */
  if (self->window) {
    if (self->no_tray) {
      markyd_window_toggle(self->window);
    } else {
      markyd_window_show(self->window);
    }
    return;
  }

  /* Initialize notes storage */
  if (!notes_init()) {
    g_printerr("Failed to initialize notes storage\n");
    return;
  }

  /* Create main window */
  self->window = markyd_window_new(self);
  self->editor = self->window->editor;

  /* Initialize system tray (unless disabled by --no-tray) */
  if (!self->no_tray) {
    tray_init(self);
  }

  /* Load notes list */
  markyd_app_refresh_notes(self);

  /* Open most recent note or create first one */
  if (self->note_paths->len > 0) {
    markyd_app_goto_note(self, 0);
  } else {
    markyd_app_new_note(self);
  }

  /* Show window on first launch (unless started minimized) */
  if (!self->start_minimized) {
    markyd_window_show(self->window);
  }
}

void markyd_app_refresh_notes(MarkydApp *self) {
  GPtrArray *paths;
  guint i;

  /* Clear existing */
  g_ptr_array_set_size(self->note_paths, 0);

  /* Get fresh list */
  paths = notes_list();
  for (i = 0; i < paths->len; i++) {
    g_ptr_array_add(self->note_paths, g_strdup(g_ptr_array_index(paths, i)));
  }
  g_ptr_array_free(paths, TRUE);

  /* Update UI */
  if (self->window) {
    markyd_window_update_counter(self->window);
    markyd_window_update_nav_sensitivity(self->window);
  }
}

void markyd_app_goto_note(MarkydApp *self, gint index) {
  gchar *content;
  const gchar *path;

  if (index < 0 || (guint)index >= self->note_paths->len) {
    return;
  }

  /* Save current note first */
  markyd_app_save_current(self);

  self->current_index = index;
  path = g_ptr_array_index(self->note_paths, index);

  content = notes_load(path);
  if (content) {
    markyd_editor_set_content(self->editor, content);
    g_free(content);
  } else {
    markyd_editor_set_content(self->editor, "");
  }

  self->modified = FALSE;

  /* Update UI */
  markyd_window_update_counter(self->window);
  markyd_window_update_nav_sensitivity(self->window);
}

void markyd_app_next_note(MarkydApp *self) {
  if (self->current_index < (gint)self->note_paths->len - 1) {
    markyd_app_goto_note(self, self->current_index + 1);
  }
}

void markyd_app_prev_note(MarkydApp *self) {
  if (self->current_index > 0) {
    markyd_app_goto_note(self, self->current_index - 1);
  }
}

void markyd_app_new_note(MarkydApp *self) {
  gchar *path;

  /* Save current first */
  markyd_app_save_current(self);

  /* Create new note */
  path = notes_create();
  if (!path) {
    g_printerr("Failed to create new note\n");
    return;
  }

  /* Refresh list and go to the new note (should be first) */
  markyd_app_refresh_notes(self);

  /* Find the new note in the list */
  for (guint i = 0; i < self->note_paths->len; i++) {
    if (g_strcmp0(g_ptr_array_index(self->note_paths, i), path) == 0) {
      self->current_index = i;
      break;
    }
  }

  g_free(path);

  /* Clear editor */
  markyd_editor_set_content(self->editor, "");
  markyd_editor_focus(self->editor);
  self->modified = TRUE;
  markyd_app_schedule_save(self);

  /* Update UI */
  markyd_window_update_counter(self->window);
  markyd_window_update_nav_sensitivity(self->window);
}

gboolean markyd_app_delete_current_note(MarkydApp *self) {
  gint count;
  gint old_index;
  gchar *path;

  if (!self || self->current_index < 0 ||
      (guint)self->current_index >= self->note_paths->len) {
    return FALSE;
  }

  count = (gint)self->note_paths->len;
  old_index = self->current_index;

  /* If there's only one note, "delete" means clear its contents. */
  if (count <= 1) {
    markyd_editor_set_content(self->editor, "");
    self->modified = TRUE;
    markyd_app_schedule_save(self);
    markyd_window_update_counter(self->window);
    markyd_window_update_nav_sensitivity(self->window);
    return TRUE;
  }

  /* Save current note first */
  markyd_app_save_current(self);

  path = g_strdup(g_ptr_array_index(self->note_paths, self->current_index));
  if (!notes_delete(path)) {
    g_free(path);
    return FALSE;
  }
  g_free(path);

  /* Refresh list and show an existing note */
  markyd_app_refresh_notes(self);

  if (self->note_paths->len == 0) {
    /* Shouldn't happen, but keep the app usable. */
    markyd_app_new_note(self);
    return TRUE;
  }

  if (old_index >= (gint)self->note_paths->len) {
    old_index = (gint)self->note_paths->len - 1;
  }
  markyd_app_goto_note(self, old_index);

  return TRUE;
}

void markyd_app_schedule_save(MarkydApp *self) {
  self->modified = TRUE;

  /* Cancel existing timeout */
  if (self->save_timeout_id > 0) {
    g_source_remove(self->save_timeout_id);
  }

  /* Schedule new save */
  self->save_timeout_id =
      g_timeout_add(AUTOSAVE_DELAY_MS, on_autosave_timeout, self);
}

static gboolean on_autosave_timeout(gpointer user_data) {
  MarkydApp *self = (MarkydApp *)user_data;

  self->save_timeout_id = 0;
  markyd_app_save_current(self);

  return G_SOURCE_REMOVE;
}

void markyd_app_save_current(MarkydApp *self) {
  gchar *content;
  const gchar *path;

  if (!self->modified || self->current_index < 0) {
    return;
  }

  path = g_ptr_array_index(self->note_paths, self->current_index);
  content = markyd_editor_get_content(self->editor);

  if (notes_save(path, content)) {
    self->modified = FALSE;
  }

  g_free(content);
}

const gchar *markyd_app_get_current_path(MarkydApp *self) {
  if (self->current_index < 0 ||
      (guint)self->current_index >= self->note_paths->len) {
    return NULL;
  }
  return g_ptr_array_index(self->note_paths, self->current_index);
}

gint markyd_app_get_note_count(MarkydApp *self) {
  return (gint)self->note_paths->len;
}
