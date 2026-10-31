#include "config.h"
#include <glib/gstdio.h>

static void write_settings(const gchar *settings) {
  GError *error = NULL;
  g_assert_true(g_file_set_contents(config_get_path(), settings, -1, &error));
  g_assert_no_error(error);
}

static void set_theme(MarkydConfig *cfg, const gchar *theme) {
  g_free(cfg->theme);
  cfg->theme = g_strdup(theme);
  config_apply_theme_defaults(cfg, cfg->dark_palette);
}

static void test_defaults_and_overrides(void) {
  MarkydConfig *cfg = config_new();
  gchar *dark_h1 = g_strdup(cfg->h1_color);
  set_theme(cfg, "light");
  g_assert_cmpstr(cfg->h1_color, !=, dark_h1);
  for (gint i = 0; i < MARKYD_ACCENT_COUNT; i++) {
    g_assert_false(cfg->accent_custom[i]);
  }
  config_set_accent(cfg, MARKYD_ACCENT_H1, "#005500");
  g_assert_true(cfg->accent_custom[MARKYD_ACCENT_H1]);
  set_theme(cfg, "dark");
  g_assert_cmpstr(cfg->h1_color, ==, "#005500");
  set_theme(cfg, "light");
  g_assert_cmpstr(cfg->h1_color, ==, "#005500");
  config_reset_accent(cfg, MARKYD_ACCENT_H1);
  g_assert_false(cfg->accent_custom[MARKYD_ACCENT_H1]);
  g_assert_cmpstr(cfg->h1_color, !=, "#005500");
  set_theme(cfg, "dark");
  g_assert_cmpstr(cfg->h1_color, ==, dark_h1);
  g_free(dark_h1);
  config_free(cfg);
}

static void test_legacy_defaults(void) {
  write_settings("[Appearance]\ntheme=light\n[Markdown]\n"
                 "h1_color=#61afef\nh2_color=#C678DD\n"
                 "h3_color=#E5C07B\nlist_bullet_color=#61AFEF\n");
  MarkydConfig *cfg = config_new();
  g_assert_true(config_load(cfg));
  for (gint i = 0; i < MARKYD_ACCENT_COUNT; i++) {
    g_assert_false(cfg->accent_custom[i]);
  }
  g_assert_cmpstr(cfg->h1_color, !=, "#61AFEF");
  g_assert_cmpstr(cfg->h3_color, !=, "#E5C07B");
  set_theme(cfg, "dark");
  g_assert_cmpstr(cfg->h1_color, ==, "#61AFEF");
  g_assert_cmpstr(cfg->h3_color, ==, "#E5C07B");
  config_free(cfg);
}

static void test_legacy_custom_colors(void) {
  write_settings("[Appearance]\ntheme=light\nfont_size=20\n"
                 "[Window]\nwidth=850\n[Markdown]\nh2_color=#123456\n"
                 "list_bullet_color=#654321\n");
  MarkydConfig *cfg = config_new();
  g_assert_true(config_load(cfg));
  g_assert_true(cfg->accent_custom[MARKYD_ACCENT_H2]);
  g_assert_true(cfg->accent_custom[MARKYD_ACCENT_LIST]);
  g_assert_false(cfg->accent_custom[MARKYD_ACCENT_H1]);
  g_assert_false(cfg->accent_custom[MARKYD_ACCENT_H3]);
  g_assert_cmpint(cfg->font_size, ==, 20);
  g_assert_cmpint(cfg->window_width, ==, 850);
  for (gint i = 0; i < 2; i++) {
    set_theme(cfg, i == 0 ? "dark" : "light");
    g_assert_cmpstr(cfg->h2_color, ==, "#123456");
    g_assert_cmpstr(cfg->list_bullet_color, ==, "#654321");
  }
  config_free(cfg);
}

static void test_roundtrip_and_old_keys(void) {
  MarkydConfig *cfg = config_new();
  /* An explicit choice matching an old default must remain custom. */
  config_set_accent(cfg, MARKYD_ACCENT_H1, "#61AFEF");
  config_set_accent(cfg, MARKYD_ACCENT_LIST, "#112233");
  set_theme(cfg, "light");
  gchar *light_h2 = g_strdup(cfg->h2_color);
  g_assert_true(config_save(cfg));

  GKeyFile *file = g_key_file_new();
  g_assert_true(g_key_file_load_from_file(file, config_get_path(), 0, NULL));
  const gchar *keys[] = {"h1_color", "h2_color", "h3_color", "list_bullet_color"};
  for (gint i = 0; i < MARKYD_ACCENT_COUNT; i++) {
    /* Existing versions can still read the same four color keys. */
    gchar *color = g_key_file_get_string(file, "Markdown", keys[i], NULL);
    g_assert_cmpstr(color, ==, config_get_accent(cfg, i));
    g_free(color);
  }
  g_key_file_free(file);
  config_free(cfg);

  cfg = config_new();
  g_assert_true(config_load(cfg));
  g_assert_true(cfg->accent_custom[MARKYD_ACCENT_H1]);
  g_assert_true(cfg->accent_custom[MARKYD_ACCENT_LIST]);
  g_assert_false(cfg->accent_custom[MARKYD_ACCENT_H2]);
  g_assert_cmpstr(cfg->h1_color, ==, "#61AFEF");
  g_assert_cmpstr(cfg->h2_color, ==, light_h2);
  set_theme(cfg, "dark");
  set_theme(cfg, "light");
  g_assert_cmpstr(cfg->h1_color, ==, "#61AFEF");
  g_assert_cmpstr(cfg->list_bullet_color, ==, "#112233");
  config_reset_accent(cfg, MARKYD_ACCENT_H1);
  g_assert_true(config_save(cfg));
  config_free(cfg);
  cfg = config_new();
  g_assert_true(config_load(cfg));
  g_assert_false(cfg->accent_custom[MARKYD_ACCENT_H1]);
  g_assert_cmpstr(cfg->h1_color, !=, "#61AFEF");
  g_free(light_h2);
  config_free(cfg);
}

static void test_invalid_flag_preserves_color(void) {
  write_settings("[Appearance]\ntheme=light\n[Markdown]\n"
                 "h1_color=#61AFEF\nh1_color_custom=invalid\n");
  MarkydConfig *cfg = config_new();
  g_assert_true(config_load(cfg));
  g_assert_true(cfg->accent_custom[MARKYD_ACCENT_H1]);
  g_assert_cmpstr(cfg->h1_color, ==, "#61AFEF");
  config_free(cfg);
}

static void test_settings_copy(void) {
  MarkydConfig *original = config_new();
  MarkydConfig *draft = config_copy(original);
  set_theme(draft, "light");
  config_set_accent(draft, MARKYD_ACCENT_H1, "#001122");
  g_assert_cmpstr(original->theme, ==, "dark");
  g_assert_cmpstr(original->h1_color, ==, "#61AFEF");
  g_assert_false(original->accent_custom[MARKYD_ACCENT_H1]);
  config_free(draft);
  config_free(original);
}

int main(int argc, char **argv) {
  GError *error = NULL;
  gchar *directory = g_dir_make_tmp("traymd-config-test-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_true(g_setenv("XDG_CONFIG_HOME", directory, TRUE));
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/config/defaults-and-overrides", test_defaults_and_overrides);
  g_test_add_func("/config/legacy-defaults", test_legacy_defaults);
  g_test_add_func("/config/legacy-custom-colors", test_legacy_custom_colors);
  g_test_add_func("/config/roundtrip-and-old-keys", test_roundtrip_and_old_keys);
  g_test_add_func("/config/invalid-flag", test_invalid_flag_preserves_color);
  g_test_add_func("/config/settings-copy", test_settings_copy);
  gint result = g_test_run();
  gchar *app_directory = g_path_get_dirname(config_get_path());
  if (g_file_test(config_get_path(), G_FILE_TEST_EXISTS)) {
    g_assert_cmpint(g_remove(config_get_path()), ==, 0);
  }
  g_assert_cmpint(g_rmdir(app_directory), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
  g_free(app_directory);
  g_free(directory);
  return result;
}
