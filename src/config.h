#ifndef MARKYD_CONFIG_H
#define MARKYD_CONFIG_H

#include <glib.h>

typedef enum {
  MARKYD_ACCENT_H1,
  MARKYD_ACCENT_H2,
  MARKYD_ACCENT_H3,
  MARKYD_ACCENT_LIST,
  MARKYD_ACCENT_COUNT
} MarkydAccent;

/* Settings structure */
typedef struct _MarkydConfig {
  /* Window geometry */
  gint window_x;
  gint window_y;
  gint window_width;
  gint window_height;
  gboolean window_maximized;

  /* Appearance */
  gchar *font_family;
  gint font_size; /* in points */
  gchar *theme;   /* "dark", "light", "system" */

  /* Markdown accent colors */
  gchar *h1_color;
  gchar *h2_color;
  gchar *h3_color;
  gchar *list_bullet_color;
  gboolean accent_custom[MARKYD_ACCENT_COUNT];
  gboolean dark_palette;

  /* Editor */
  gboolean line_numbers;
  gboolean word_wrap;
} MarkydConfig;

/* Global config instance */
extern MarkydConfig *config;

/* Lifecycle */
MarkydConfig *config_new(void);
MarkydConfig *config_copy(const MarkydConfig *cfg);
void config_free(MarkydConfig *cfg);

/* Custom colors stay fixed; automatic accents follow the resolved theme. */
const gchar *config_get_accent(const MarkydConfig *cfg, MarkydAccent accent);
void config_set_accent(MarkydConfig *cfg, MarkydAccent accent, const gchar *color);
void config_reset_accent(MarkydConfig *cfg, MarkydAccent accent);
gboolean config_apply_theme_defaults(MarkydConfig *cfg, gboolean dark);

/* Load/Save */
gboolean config_load(MarkydConfig *cfg);
gboolean config_save(MarkydConfig *cfg);

/* Get config file path */
const gchar *config_get_path(void);

#endif /* MARKYD_CONFIG_H */
