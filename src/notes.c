#include "notes.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static gchar *notes_dir = NULL;

gboolean notes_init(void) {
  const gchar *data_dir;
  gchar *old_app_dir;
  gchar *new_app_dir;
  gchar *new_notes_dir;

  /* Build path: ~/.local/share/traymd/notes */
  data_dir = g_get_user_data_dir();
  old_app_dir = g_build_filename(data_dir, "markyd", NULL);
  new_app_dir = g_build_filename(data_dir, "traymd", NULL);
  new_notes_dir = g_build_filename(new_app_dir, "notes", NULL);

  /*
   * Rebrand migration: move ~/.local/share/markyd -> ~/.local/share/traymd if
   * the new directory doesn't exist yet.
   */
  if (!g_file_test(new_app_dir, G_FILE_TEST_EXISTS) &&
      g_file_test(old_app_dir, G_FILE_TEST_EXISTS)) {
    if (g_rename(old_app_dir, new_app_dir) != 0) {
      g_printerr("Failed to migrate data directory: %s\n", g_strerror(errno));
    }
  }

  notes_dir = g_strdup(new_notes_dir);
  g_free(old_app_dir);
  g_free(new_app_dir);
  g_free(new_notes_dir);

  /* Create directory if it doesn't exist */
  if (g_mkdir_with_parents(notes_dir, 0755) != 0) {
    g_printerr("Failed to create notes directory: %s\n", g_strerror(errno));
    return FALSE;
  }

  return TRUE;
}

const gchar *notes_get_dir(void) { return notes_dir; }

/* Compare function for sorting by mtime (newest first) */
static gint compare_by_mtime(gconstpointer a, gconstpointer b) {
  const gchar *path_a = *(const gchar **)a;
  const gchar *path_b = *(const gchar **)b;
  struct stat stat_a, stat_b;

  if (stat(path_a, &stat_a) != 0)
    return 1;
  if (stat(path_b, &stat_b) != 0)
    return -1;

  /* Newest first (descending order) */
  if (stat_b.st_mtime > stat_a.st_mtime)
    return 1;
  if (stat_b.st_mtime < stat_a.st_mtime)
    return -1;
  return 0;
}

GPtrArray *notes_list(void) {
  GPtrArray *paths;
  GDir *dir;
  const gchar *filename;
  GError *error = NULL;

  paths = g_ptr_array_new_with_free_func(g_free);

  dir = g_dir_open(notes_dir, 0, &error);
  if (!dir) {
    g_printerr("Failed to open notes directory: %s\n", error->message);
    g_error_free(error);
    return paths;
  }

  while ((filename = g_dir_read_name(dir)) != NULL) {
    /* Only include .md files */
    if (g_str_has_suffix(filename, ".md")) {
      gchar *path = g_build_filename(notes_dir, filename, NULL);
      g_ptr_array_add(paths, path);
    }
  }

  g_dir_close(dir);

  /* Sort by modification time (newest first) */
  g_ptr_array_sort(paths, compare_by_mtime);

  return paths;
}

gchar *notes_create(void) {
  gchar *filename;
  gchar *path;
  time_t now;
  struct tm *tm_info;
  gchar timestamp[32];
  FILE *fp;

  /* Generate filename from timestamp */
  now = time(NULL);
  tm_info = localtime(&now);
  strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);

  filename = g_strdup_printf("%s.md", timestamp);
  path = g_build_filename(notes_dir, filename, NULL);
  g_free(filename);

  /* Create empty file */
  fp = fopen(path, "w");
  if (!fp) {
    g_printerr("Failed to create note: %s\n", g_strerror(errno));
    g_free(path);
    return NULL;
  }
  fclose(fp);

  return path;
}

gchar *notes_load(const gchar *path) {
  gchar *content = NULL;
  GError *error = NULL;

  if (!g_file_get_contents(path, &content, NULL, &error)) {
    g_printerr("Failed to load note: %s\n", error->message);
    g_error_free(error);
    return NULL;
  }

  return content;
}

gboolean notes_save(const gchar *path, const gchar *content) {
  GError *error = NULL;

  if (!g_file_set_contents(path, content, -1, &error)) {
    g_printerr("Failed to save note: %s\n", error->message);
    g_error_free(error);
    return FALSE;
  }

  return TRUE;
}

gboolean notes_delete(const gchar *path) {
  if (!path) {
    return FALSE;
  }

  if (g_remove(path) != 0) {
    g_printerr("Failed to delete note '%s': %s\n", path, g_strerror(errno));
    return FALSE;
  }

  return TRUE;
}

gint notes_count(void) {
  GPtrArray *paths = notes_list();
  gint count = paths->len;
  g_ptr_array_free(paths, TRUE);
  return count;
}
