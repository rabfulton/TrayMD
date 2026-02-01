#ifndef MARKYD_NOTES_H
#define MARKYD_NOTES_H

#include <glib.h>

/* Initialize notes storage directory */
gboolean notes_init(void);

/* Get storage directory path */
const gchar *notes_get_dir(void);

/* Get list of all note paths (sorted by mtime, newest first) */
GPtrArray *notes_list(void);

/* Create a new note, returns path (caller must free) */
gchar *notes_create(void);

/* Load note content (caller must free) */
gchar *notes_load(const gchar *path);

/* Save note content */
gboolean notes_save(const gchar *path, const gchar *content);

/* Delete a note file */
gboolean notes_delete(const gchar *path);

/* Get note count */
gint notes_count(void);

#endif /* MARKYD_NOTES_H */
