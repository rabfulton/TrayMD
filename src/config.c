#include "config.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

/* Global config instance */
MarkydConfig *config = NULL;

static gchar *config_path = NULL;

static const gchar *accent_keys[MARKYD_ACCENT_COUNT] = {
    "h1_color", "h2_color", "h3_color", "list_bullet_color"};
static const gchar *accent_dark[MARKYD_ACCENT_COUNT] = {
    "#61AFEF", "#C678DD", "#E5C07B", "#61AFEF"};
static const gchar *accent_light[MARKYD_ACCENT_COUNT] = {
    "#0969DA", "#8250DF", "#8A4600", "#0969DA"};

static gchar **accent_slot(MarkydConfig *cfg, MarkydAccent accent) {
  switch (accent) {
  case MARKYD_ACCENT_H1: return &cfg->h1_color;
  case MARKYD_ACCENT_H2: return &cfg->h2_color;
  case MARKYD_ACCENT_H3: return &cfg->h3_color;
  case MARKYD_ACCENT_LIST: return &cfg->list_bullet_color;
  default: return NULL;
  }
}

const gchar *config_get_accent(const MarkydConfig *cfg, MarkydAccent accent) {
  switch (accent) {
  case MARKYD_ACCENT_H1: return cfg->h1_color;
  case MARKYD_ACCENT_H2: return cfg->h2_color;
  case MARKYD_ACCENT_H3: return cfg->h3_color;
  case MARKYD_ACCENT_LIST: return cfg->list_bullet_color;
  default: return NULL;
  }
}

gboolean config_apply_theme_defaults(MarkydConfig *cfg, gboolean dark) {
  if (g_strcmp0(cfg->theme, "light") == 0) {
    dark = FALSE;
  } else if (g_strcmp0(cfg->theme, "dark") == 0) {
    dark = TRUE;
  }
  cfg->dark_palette = dark;
  const gchar *const *defaults = dark ? accent_dark : accent_light;
  gboolean changed = FALSE;
  for (gint i = 0; i < MARKYD_ACCENT_COUNT; i++) {
    gchar **slot = accent_slot(cfg, i);
    if (!cfg->accent_custom[i] && g_strcmp0(*slot, defaults[i]) != 0) {
      g_free(*slot);
      *slot = g_strdup(defaults[i]);
      changed = TRUE;
    }
  }
  return changed;
}

void config_set_accent(MarkydConfig *cfg, MarkydAccent accent, const gchar *color) {
  gchar **slot = accent_slot(cfg, accent);
  g_return_if_fail(slot != NULL && color != NULL);
  gchar *copy = g_strdup(color);
  g_free(*slot);
  *slot = copy;
  cfg->accent_custom[accent] = TRUE;
}

void config_reset_accent(MarkydConfig *cfg, MarkydAccent accent) {
  g_return_if_fail(accent >= 0 && accent < MARKYD_ACCENT_COUNT);
  cfg->accent_custom[accent] = FALSE;
  config_apply_theme_defaults(cfg, cfg->dark_palette);
}

MarkydConfig *config_new(void) {
  MarkydConfig *cfg = g_new0(MarkydConfig, 1);

  /* Default values */
  cfg->window_x = -1;
  cfg->window_y = -1;
  cfg->window_width = 600;
  cfg->window_height = 700;
  cfg->window_maximized = FALSE;

  cfg->font_family = g_strdup("Cantarell");
  cfg->font_size = 16;
  cfg->theme = g_strdup("dark");

  config_apply_theme_defaults(cfg, TRUE);

  cfg->line_numbers = FALSE;
  cfg->word_wrap = TRUE;

  return cfg;
}

MarkydConfig *config_copy(const MarkydConfig *cfg) {
  MarkydConfig *copy = g_new(MarkydConfig, 1);
  *copy = *cfg;
  copy->font_family = g_strdup(cfg->font_family);
  copy->theme = g_strdup(cfg->theme);
  copy->h1_color = g_strdup(cfg->h1_color);
  copy->h2_color = g_strdup(cfg->h2_color);
  copy->h3_color = g_strdup(cfg->h3_color);
  copy->list_bullet_color = g_strdup(cfg->list_bullet_color);
  return copy;
}

void config_free(MarkydConfig *cfg) {
  if (!cfg)
    return;
  g_free(cfg->font_family);
  g_free(cfg->theme);
  g_free(cfg->h1_color);
  g_free(cfg->h2_color);
  g_free(cfg->h3_color);
  g_free(cfg->list_bullet_color);
  g_free(cfg);
}

const gchar *config_get_path(void) {
  if (!config_path) {
    const gchar *config_dir = g_get_user_config_dir();
    gchar *old_app_dir = g_build_filename(config_dir, "markyd", NULL);
    gchar *new_app_dir = g_build_filename(config_dir, "traymd", NULL);

    /*
     * Rebrand migration: move ~/.config/markyd -> ~/.config/traymd if the new
     * directory doesn't exist yet.
     */
    if (!g_file_test(new_app_dir, G_FILE_TEST_EXISTS) &&
        g_file_test(old_app_dir, G_FILE_TEST_EXISTS)) {
      if (g_rename(old_app_dir, new_app_dir) != 0) {
        g_printerr("Failed to migrate config directory: %s\n",
                   g_strerror(errno));
      }
    }

    g_mkdir_with_parents(new_app_dir, 0755);
    config_path = g_build_filename(new_app_dir, "config.ini", NULL);

    g_free(old_app_dir);
    g_free(new_app_dir);
  }
  return config_path;
}

gboolean config_load(MarkydConfig *cfg) {
  GKeyFile *keyfile;
  GError *error = NULL;
  const gchar *path;

  path = config_get_path();
  keyfile = g_key_file_new();

  if (!g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, &error)) {
    /* File doesn't exist or is invalid - use defaults */
    g_clear_error(&error);
    g_key_file_free(keyfile);
    return FALSE;
  }

  /* Window geometry */
  if (g_key_file_has_key(keyfile, "Window", "x", NULL))
    cfg->window_x = g_key_file_get_integer(keyfile, "Window", "x", NULL);
  if (g_key_file_has_key(keyfile, "Window", "y", NULL))
    cfg->window_y = g_key_file_get_integer(keyfile, "Window", "y", NULL);
  if (g_key_file_has_key(keyfile, "Window", "width", NULL))
    cfg->window_width =
        g_key_file_get_integer(keyfile, "Window", "width", NULL);
  if (g_key_file_has_key(keyfile, "Window", "height", NULL))
    cfg->window_height =
        g_key_file_get_integer(keyfile, "Window", "height", NULL);
  if (g_key_file_has_key(keyfile, "Window", "maximized", NULL))
    cfg->window_maximized =
        g_key_file_get_boolean(keyfile, "Window", "maximized", NULL);

  /* Appearance */
  if (g_key_file_has_key(keyfile, "Appearance", "font_family", NULL)) {
    g_free(cfg->font_family);
    cfg->font_family =
        g_key_file_get_string(keyfile, "Appearance", "font_family", NULL);
  }
  if (g_key_file_has_key(keyfile, "Appearance", "font_size", NULL))
    cfg->font_size =
        g_key_file_get_integer(keyfile, "Appearance", "font_size", NULL);
  if (g_key_file_has_key(keyfile, "Appearance", "theme", NULL)) {
    g_free(cfg->theme);
    cfg->theme = g_key_file_get_string(keyfile, "Appearance", "theme", NULL);
  }

  /* Markdown accents */
  for (gint i = 0; i < MARKYD_ACCENT_COUNT; i++) {
    gchar **slot = accent_slot(cfg, i);
    gboolean stored_color = g_key_file_has_key(keyfile, "Markdown", accent_keys[i], NULL);
    if (stored_color) {
      g_free(*slot);
      *slot = g_key_file_get_string(keyfile, "Markdown", accent_keys[i], NULL);
    }
    gchar *custom_key = g_strconcat(accent_keys[i], "_custom", NULL);
    if (g_key_file_has_key(keyfile, "Markdown", custom_key, NULL)) {
      GError *flag_error = NULL;
      gboolean custom = g_key_file_get_boolean(keyfile, "Markdown", custom_key,
                                                &flag_error);
      /* An invalid flag must not discard a potentially custom color. */
      cfg->accent_custom[i] = flag_error ? TRUE : custom;
      g_clear_error(&flag_error);
    } else {
      /* Old files saved defaults too, without identifying user choices. */
      cfg->accent_custom[i] = stored_color &&
          g_ascii_strcasecmp(*slot, accent_dark[i]) != 0;
    }
    g_free(custom_key);
  }
  config_apply_theme_defaults(cfg, cfg->dark_palette);

  /* Editor */
  if (g_key_file_has_key(keyfile, "Editor", "word_wrap", NULL))
    cfg->word_wrap =
        g_key_file_get_boolean(keyfile, "Editor", "word_wrap", NULL);

  g_key_file_free(keyfile);
  return TRUE;
}

gboolean config_save(MarkydConfig *cfg) {
  GKeyFile *keyfile;
  GError *error = NULL;
  const gchar *path;
  gchar *data;
  gsize length;

  keyfile = g_key_file_new();
  path = config_get_path();

  /* Window geometry */
  g_key_file_set_integer(keyfile, "Window", "x", cfg->window_x);
  g_key_file_set_integer(keyfile, "Window", "y", cfg->window_y);
  g_key_file_set_integer(keyfile, "Window", "width", cfg->window_width);
  g_key_file_set_integer(keyfile, "Window", "height", cfg->window_height);
  g_key_file_set_boolean(keyfile, "Window", "maximized", cfg->window_maximized);

  /* Appearance */
  g_key_file_set_string(keyfile, "Appearance", "font_family", cfg->font_family);
  g_key_file_set_integer(keyfile, "Appearance", "font_size", cfg->font_size);
  g_key_file_set_string(keyfile, "Appearance", "theme", cfg->theme);

  /* Markdown accents */
  config_apply_theme_defaults(cfg, cfg->dark_palette);
  for (gint i = 0; i < MARKYD_ACCENT_COUNT; i++) {
    /* Keep the original keys readable by older TrayMD versions. */
    g_key_file_set_string(keyfile, "Markdown", accent_keys[i],
                           config_get_accent(cfg, i));
    gchar *custom_key = g_strconcat(accent_keys[i], "_custom", NULL);
    g_key_file_set_boolean(keyfile, "Markdown", custom_key, cfg->accent_custom[i]);
    g_free(custom_key);
  }

  /* Editor */
  g_key_file_set_boolean(keyfile, "Editor", "word_wrap", cfg->word_wrap);

  data = g_key_file_to_data(keyfile, &length, &error);
  if (error) {
    g_printerr("Failed to serialize config: %s\n", error->message);
    g_error_free(error);
    g_key_file_free(keyfile);
    return FALSE;
  }

  if (!g_file_set_contents(path, data, length, &error)) {
    g_printerr("Failed to save config: %s\n", error->message);
    g_error_free(error);
    g_free(data);
    g_key_file_free(keyfile);
    return FALSE;
  }

  g_free(data);
  g_key_file_free(keyfile);
  return TRUE;
}
