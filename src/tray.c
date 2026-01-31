#include "tray.h"
#include "app.h"
#include "config.h"
#include "editor.h"
#include "markdown.h"
#include "window.h"
#include <libayatana-appindicator/app-indicator.h>
#include <unistd.h>

static AppIndicator *indicator = NULL;
static GtkStatusIcon *status_icon = NULL;
static GtkWidget *menu = NULL;
static GtkWidget *item_show = NULL;
static MarkydApp *tray_app = NULL;

static void on_show_activate(GtkMenuItem *item, gpointer user_data);
static void on_new_activate(GtkMenuItem *item, gpointer user_data);
static void on_settings_activate(GtkMenuItem *item, gpointer user_data);
static void on_quit_activate(GtkMenuItem *item, gpointer user_data);
static void on_status_icon_activate(GtkStatusIcon *icon, gpointer user_data);
static void on_status_icon_popup_menu(GtkStatusIcon *icon, guint button,
                                      guint activate_time,
                                      gpointer user_data);

/* Settings dialog widgets */
static GtkWidget *create_settings_dialog(MarkydApp *app);

/* Autostart management */
static gchar *get_autostart_path(void);
static gboolean is_autostart_enabled(void);
static void set_autostart_enabled(gboolean enabled);

void tray_init(MarkydApp *app) {
  GtkWidget *item_new;
  GtkWidget *item_settings;
  GtkWidget *item_separator;
  GtkWidget *item_quit;

  tray_app = app;

  /* Create menu */
  menu = gtk_menu_new();

  /* Show/Hide item - this will be the left-click action */
  item_show = gtk_menu_item_new_with_label("Show/Hide");
  g_signal_connect(item_show, "activate", G_CALLBACK(on_show_activate), app);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_show);

  /* New note item */
  item_new = gtk_menu_item_new_with_label("New Note");
  g_signal_connect(item_new, "activate", G_CALLBACK(on_new_activate), app);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_new);

  /* Separator */
  item_separator = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_separator);

  /* Settings item */
  item_settings = gtk_menu_item_new_with_label("Settings...");
  g_signal_connect(item_settings, "activate", G_CALLBACK(on_settings_activate),
                   app);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_settings);

  /* Separator */
  item_separator = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_separator);

  /* Quit item */
  item_quit = gtk_menu_item_new_with_label("Quit");
  g_signal_connect(item_quit, "activate", G_CALLBACK(on_quit_activate), app);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_quit);

  gtk_widget_show_all(menu);

  if (app->tray_backend == MARKYD_TRAY_BACKEND_APPINDICATOR) {
    /* Create indicator */
    indicator = app_indicator_new("traymd", "accessories-text-editor",
                                  APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_title(indicator, "TrayMD");

    app_indicator_set_menu(indicator, GTK_MENU(menu));

    /*
     * Note: AppIndicator reserves primary (left) click for showing the menu in
     * many desktop environments. The "secondary activate target" is typically
     * triggered by middle click.
     */
    app_indicator_set_secondary_activate_target(indicator, item_show);
  } else {
    status_icon =
        gtk_status_icon_new_from_icon_name("accessories-text-editor");
    gtk_status_icon_set_tooltip_text(status_icon, "TrayMD");
    gtk_status_icon_set_visible(status_icon, TRUE);

    g_signal_connect(status_icon, "activate",
                     G_CALLBACK(on_status_icon_activate), app);
    g_signal_connect(status_icon, "popup-menu",
                     G_CALLBACK(on_status_icon_popup_menu), app);
  }
}

void tray_cleanup(void) {
  if (indicator) {
    g_object_unref(indicator);
    indicator = NULL;
  }

  if (status_icon) {
    g_object_unref(status_icon);
    status_icon = NULL;
  }

  if (menu) {
    gtk_widget_destroy(menu);
    menu = NULL;
  }

  tray_app = NULL;
}

static void on_show_activate(GtkMenuItem *item, gpointer user_data) {
  MarkydApp *app = (MarkydApp *)user_data;
  (void)item;

  markyd_window_toggle(app->window);
}

static void on_status_icon_activate(GtkStatusIcon *icon, gpointer user_data) {
  MarkydApp *app = (MarkydApp *)user_data;
  (void)icon;

  markyd_window_toggle(app->window);
}

static void on_status_icon_popup_menu(GtkStatusIcon *icon, guint button,
                                      guint activate_time,
                                      gpointer user_data) {
  (void)user_data;

  gtk_menu_popup(GTK_MENU(menu), NULL, NULL, gtk_status_icon_position_menu,
                 icon, button, activate_time);
}

static void on_new_activate(GtkMenuItem *item, gpointer user_data) {
  MarkydApp *app = (MarkydApp *)user_data;
  (void)item;

  markyd_app_new_note(app);
  markyd_window_show(app->window);
}

static void on_settings_activate(GtkMenuItem *item, gpointer user_data) {
  MarkydApp *app = (MarkydApp *)user_data;
  GtkWidget *dialog;
  gint response;

  (void)item;

  dialog = create_settings_dialog(app);
  response = gtk_dialog_run(GTK_DIALOG(dialog));

  if (response == GTK_RESPONSE_APPLY || response == GTK_RESPONSE_OK) {
    /* Settings are applied in the dialog callbacks */
    config_save(config);
    markyd_window_apply_css(app->window);
    markdown_update_accent_tags(app->editor->buffer);
    markyd_editor_refresh(app->editor);
  }

  gtk_widget_destroy(dialog);
}

static void on_quit_activate(GtkMenuItem *item, gpointer user_data) {
  MarkydApp *app = (MarkydApp *)user_data;
  (void)item;

  /* Save config before quitting */
  config_save(config);

  /* Save current note */
  markyd_app_save_current(app);

  g_application_quit(G_APPLICATION(app->gtk_app));
}

/* Autostart management */
static gchar *get_autostart_path(void) {
  const gchar *config_dir = g_get_user_config_dir();
  gchar *autostart_dir = g_build_filename(config_dir, "autostart", NULL);
  g_mkdir_with_parents(autostart_dir, 0755);
  gchar *path = g_build_filename(autostart_dir, "traymd.desktop", NULL);
  g_free(autostart_dir);
  return path;
}

static gboolean is_autostart_enabled(void) {
  gchar *path_new = get_autostart_path();
  gboolean exists = g_file_test(path_new, G_FILE_TEST_EXISTS);

  if (!exists) {
    const gchar *config_dir = g_get_user_config_dir();
    gchar *autostart_dir = g_build_filename(config_dir, "autostart", NULL);
    gchar *path_old = g_build_filename(autostart_dir, "markyd.desktop", NULL);
    exists = g_file_test(path_old, G_FILE_TEST_EXISTS);
    g_free(path_old);
    g_free(autostart_dir);
  }

  g_free(path_new);
  return exists;
}

static void set_autostart_enabled(gboolean enabled) {
  gchar *path_new = get_autostart_path();
  const gchar *config_dir = g_get_user_config_dir();
  gchar *autostart_dir = g_build_filename(config_dir, "autostart", NULL);
  gchar *path_old = g_build_filename(autostart_dir, "markyd.desktop", NULL);

  if (enabled) {
    /* Create autostart desktop file */
    const gchar *desktop_content = "[Desktop Entry]\n"
                                   "Type=Application\n"
                                   "Name=TrayMD\n"
                                   "Comment=Lightweight markdown notes\n"
                                   "Exec=traymd --minimized\n"
                                   "Icon=accessories-text-editor\n"
                                   "Categories=Utility;TextEditor;\n"
                                   "X-GNOME-Autostart-enabled=true\n";

    g_file_set_contents(path_new, desktop_content, -1, NULL);
  } else {
    /* Remove autostart file */
    unlink(path_new);
    unlink(path_old);
  }

  g_free(path_new);
  g_free(path_old);
  g_free(autostart_dir);
}

/* Settings dialog implementation */
static void on_font_size_changed(GtkSpinButton *spin, gpointer user_data) {
  (void)user_data;
  config->font_size = gtk_spin_button_get_value_as_int(spin);
}

static void on_font_family_changed(GtkComboBoxText *combo, gpointer user_data) {
  (void)user_data;
  g_free(config->font_family);
  config->font_family = gtk_combo_box_text_get_active_text(combo);
}

static void on_theme_changed(GtkComboBoxText *combo, gpointer user_data) {
  (void)user_data;
  g_free(config->theme);
  config->theme = gtk_combo_box_text_get_active_text(combo);
}

static gchar *rgba_to_hex(const GdkRGBA *rgba) {
  guint r = (guint)(CLAMP(rgba->red, 0.0, 1.0) * 255.0 + 0.5);
  guint g = (guint)(CLAMP(rgba->green, 0.0, 1.0) * 255.0 + 0.5);
  guint b = (guint)(CLAMP(rgba->blue, 0.0, 1.0) * 255.0 + 0.5);
  return g_strdup_printf("#%02X%02X%02X", r, g, b);
}

static void init_color_button(GtkColorButton *btn, const gchar *color_str) {
  GdkRGBA rgba;
  if (color_str && gdk_rgba_parse(&rgba, color_str)) {
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(btn), &rgba);
  }
}

static void on_color_set(GtkColorButton *btn, gpointer user_data) {
  gchar **target = (gchar **)user_data;
  GdkRGBA rgba;

  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &rgba);
  g_free(*target);
  *target = rgba_to_hex(&rgba);
}

static void on_autostart_toggled(GtkToggleButton *toggle, gpointer user_data) {
  (void)user_data;
  set_autostart_enabled(gtk_toggle_button_get_active(toggle));
}

static GtkWidget *create_settings_dialog(MarkydApp *app) {
  GtkWidget *dialog;
  GtkWidget *content;
  GtkWidget *grid;
  GtkWidget *label;
  GtkWidget *font_family_combo;
  GtkWidget *font_size_spin;
  GtkWidget *theme_combo;
  GtkWidget *autostart_check;
  GtkWidget *h1_color_btn;
  GtkWidget *h2_color_btn;
  GtkWidget *h3_color_btn;
  GtkWidget *bullet_color_btn;
  gint row = 0;

  dialog = gtk_dialog_new_with_buttons(
      "TrayMD Settings", GTK_WINDOW(app->window->window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
      GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_APPLY, NULL);

  gtk_window_set_default_size(GTK_WINDOW(dialog), 400, -1);

  content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content), 16);
  /* Add a little breathing room above the dialog action buttons. */
  gtk_widget_set_margin_bottom(content, 12);

  grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_container_add(GTK_CONTAINER(content), grid);

  /* Theme selection */
  label = gtk_label_new("Theme:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

  theme_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "dark");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "light");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "system");

  /* Set active based on config */
  if (g_strcmp0(config->theme, "light") == 0)
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo), 1);
  else if (g_strcmp0(config->theme, "system") == 0)
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo), 2);
  else
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo), 0);

  g_signal_connect(theme_combo, "changed", G_CALLBACK(on_theme_changed), NULL);
  gtk_widget_set_hexpand(theme_combo, TRUE);
  gtk_grid_attach(GTK_GRID(grid), theme_combo, 1, row++, 1, 1);

  /* Font family */
  label = gtk_label_new("Font:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

  font_family_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Cantarell");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Inter");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Noto Sans");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Ubuntu");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Roboto");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(font_family_combo),
                                 "Monospace");

  /* Find and set active font */
  gint font_idx = 0;
  const gchar *fonts[] = {"Cantarell", "Inter",     "Noto Sans", "Ubuntu",
                          "Roboto",    "Monospace", NULL};
  for (gint i = 0; fonts[i]; i++) {
    if (g_strcmp0(config->font_family, fonts[i]) == 0) {
      font_idx = i;
      break;
    }
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(font_family_combo), font_idx);

  g_signal_connect(font_family_combo, "changed",
                   G_CALLBACK(on_font_family_changed), NULL);
  gtk_widget_set_hexpand(font_family_combo, TRUE);
  gtk_grid_attach(GTK_GRID(grid), font_family_combo, 1, row++, 1, 1);

  /* Font size */
  label = gtk_label_new("Font Size:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

  font_size_spin = gtk_spin_button_new_with_range(8, 48, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(font_size_spin), config->font_size);
  g_signal_connect(font_size_spin, "value-changed",
                   G_CALLBACK(on_font_size_changed), NULL);
  gtk_grid_attach(GTK_GRID(grid), font_size_spin, 1, row++, 1, 1);

  /* Separator */
  GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_attach(GTK_GRID(grid), separator, 0, row++, 2, 1);

  /* Markdown accent colors */
  label = gtk_label_new("Heading 1:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  h1_color_btn = gtk_color_button_new();
  init_color_button(GTK_COLOR_BUTTON(h1_color_btn), config->h1_color);
  gtk_widget_set_halign(h1_color_btn, GTK_ALIGN_START);
  g_signal_connect(h1_color_btn, "color-set", G_CALLBACK(on_color_set),
                   &config->h1_color);
  gtk_grid_attach(GTK_GRID(grid), h1_color_btn, 1, row++, 1, 1);

  label = gtk_label_new("Heading 2:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  h2_color_btn = gtk_color_button_new();
  init_color_button(GTK_COLOR_BUTTON(h2_color_btn), config->h2_color);
  gtk_widget_set_halign(h2_color_btn, GTK_ALIGN_START);
  g_signal_connect(h2_color_btn, "color-set", G_CALLBACK(on_color_set),
                   &config->h2_color);
  gtk_grid_attach(GTK_GRID(grid), h2_color_btn, 1, row++, 1, 1);

  label = gtk_label_new("Heading 3:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  h3_color_btn = gtk_color_button_new();
  init_color_button(GTK_COLOR_BUTTON(h3_color_btn), config->h3_color);
  gtk_widget_set_halign(h3_color_btn, GTK_ALIGN_START);
  g_signal_connect(h3_color_btn, "color-set", G_CALLBACK(on_color_set),
                   &config->h3_color);
  gtk_grid_attach(GTK_GRID(grid), h3_color_btn, 1, row++, 1, 1);

  label = gtk_label_new("List bullet:");
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  bullet_color_btn = gtk_color_button_new();
  init_color_button(GTK_COLOR_BUTTON(bullet_color_btn),
                    config->list_bullet_color);
  gtk_widget_set_halign(bullet_color_btn, GTK_ALIGN_START);
  g_signal_connect(bullet_color_btn, "color-set", G_CALLBACK(on_color_set),
                   &config->list_bullet_color);
  gtk_grid_attach(GTK_GRID(grid), bullet_color_btn, 1, row++, 1, 1);

  /* Separator */
  separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_attach(GTK_GRID(grid), separator, 0, row++, 2, 1);

  /* Autostart checkbox */
  autostart_check =
      gtk_check_button_new_with_label("Start automatically on login");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autostart_check),
                               is_autostart_enabled());
  gtk_widget_set_margin_top(autostart_check, 4);
  gtk_widget_set_margin_bottom(autostart_check, 8);
  g_signal_connect(autostart_check, "toggled", G_CALLBACK(on_autostart_toggled),
                   NULL);
  gtk_grid_attach(GTK_GRID(grid), autostart_check, 0, row++, 2, 1);

  gtk_widget_show_all(dialog);

  return dialog;
}
