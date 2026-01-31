#include "window.h"
#include "app.h"
#include "config.h"
#include "editor.h"

static void on_new_clicked(GtkButton *button, gpointer user_data);
static void on_prev_clicked(GtkButton *button, gpointer user_data);
static void on_next_clicked(GtkButton *button, gpointer user_data);
static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event,
                                gpointer user_data);
static gboolean on_configure_event(GtkWidget *widget, GdkEventConfigure *event,
                                   gpointer user_data);
static gboolean on_window_state_event(GtkWidget *widget,
                                      GdkEventWindowState *event,
                                      gpointer user_data);

MarkydWindow *markyd_window_new(MarkydApp *app) {
  MarkydWindow *self = g_new0(MarkydWindow, 1);
  GtkWidget *nav_box;

  self->app = app;

  /* Create main window */
  self->window = gtk_application_window_new(app->gtk_app);
  gtk_window_set_title(GTK_WINDOW(self->window), "TrayMD");

  /* Set size from config */
  gtk_window_set_default_size(GTK_WINDOW(self->window), config->window_width,
                              config->window_height);

  /* Restore position if saved */
  if (config->window_x >= 0 && config->window_y >= 0) {
    gtk_window_move(GTK_WINDOW(self->window), config->window_x,
                    config->window_y);
  }

  /* Restore maximized state */
  if (config->window_maximized) {
    gtk_window_maximize(GTK_WINDOW(self->window));
  }

  /* Hide instead of destroy on close */
  g_signal_connect(self->window, "delete-event", G_CALLBACK(on_delete_event),
                   self);

  /* Track window geometry changes */
  g_signal_connect(self->window, "configure-event",
                   G_CALLBACK(on_configure_event), self);
  g_signal_connect(self->window, "window-state-event",
                   G_CALLBACK(on_window_state_event), self);

  /* Create header bar */
  self->header_bar = gtk_header_bar_new();
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(self->header_bar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(self->header_bar), "TrayMD");
  gtk_window_set_titlebar(GTK_WINDOW(self->window), self->header_bar);

  /* New note button */
  self->btn_new = gtk_button_new_from_icon_name("document-new-symbolic",
                                                GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(self->btn_new, "New Note");
  g_signal_connect(self->btn_new, "clicked", G_CALLBACK(on_new_clicked), self);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(self->header_bar), self->btn_new);

  /* Navigation box */
  nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_style_context_add_class(gtk_widget_get_style_context(nav_box), "linked");

  self->btn_prev = gtk_button_new_from_icon_name("go-previous-symbolic",
                                                 GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(self->btn_prev, "Previous Note");
  g_signal_connect(self->btn_prev, "clicked", G_CALLBACK(on_prev_clicked),
                   self);
  gtk_box_pack_start(GTK_BOX(nav_box), self->btn_prev, FALSE, FALSE, 0);

  self->btn_next =
      gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(self->btn_next, "Next Note");
  g_signal_connect(self->btn_next, "clicked", G_CALLBACK(on_next_clicked),
                   self);
  gtk_box_pack_start(GTK_BOX(nav_box), self->btn_next, FALSE, FALSE, 0);

  gtk_header_bar_pack_start(GTK_HEADER_BAR(self->header_bar), nav_box);

  /* Note counter label */
  self->lbl_counter = gtk_label_new("0 / 0");
  gtk_header_bar_pack_end(GTK_HEADER_BAR(self->header_bar), self->lbl_counter);

  /* Scrolled window for editor - no extra margins */
  self->scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->scroll),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(self->window), self->scroll);

  /* Create editor */
  self->editor = markyd_editor_new(app);
  gtk_container_add(GTK_CONTAINER(self->scroll),
                    markyd_editor_get_widget(self->editor));

  /* Apply CSS for better appearance - using config font size */
  markyd_window_apply_css(self);

  /*
   * Make children visible without mapping the top-level window yet.
   * Showing and immediately hiding the top-level window causes initial
   * configure events which can overwrite the restored position.
   */
  gtk_widget_show_all(self->header_bar);
  gtk_widget_show_all(self->scroll);

  return self;
}

void markyd_window_apply_css(MarkydWindow *self) {
  static GtkCssProvider *css = NULL;
  gchar *css_str;
  const gchar *bg;
  const gchar *fg;
  const gchar *sel_bg;

  if (css) {
    gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(),
                                                 GTK_STYLE_PROVIDER(css));
    g_object_unref(css);
  }

  css = gtk_css_provider_new();

  /* Build CSS with config values */
  if (g_strcmp0(config->theme, "light") == 0) {
    bg = "#ffffff";
    fg = "#111111";
    sel_bg = "#cfe3ff";
  } else if (g_strcmp0(config->theme, "dark") == 0) {
    bg = "#1e1e1e";
    fg = "#e8e8e8";
    sel_bg = "#264f78";
  } else {
    bg = "@theme_base_color";
    fg = "@theme_text_color";
    sel_bg = "@theme_selected_bg_color";
  }

  css_str = g_strdup_printf(
      "textview {"
      "  font-family: '%s', 'Inter', 'Noto Sans', sans-serif;"
      "  font-size: %dpt;"
      "  padding: 0px;"
      "  background-color: %s;"
      "  color: %s;"
      "}"
      "textview text {"
      "  background-color: %s;"
      "  color: %s;"
      "}"
      "textview text selection {"
      "  background-color: %s;"
      "}"
      "scrolledwindow {"
      "  background-color: %s;"
      "  border: none;"
      "}"
      "window {"
      "  background-color: %s;"
      "}",
      config->font_family, config->font_size, bg, fg, bg, fg, sel_bg, bg, bg);

  gtk_css_provider_load_from_data(css, css_str, -1, NULL);
  g_free(css_str);

  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  (void)self;
}

void markyd_window_free(MarkydWindow *self) {
  if (!self)
    return;

  if (self->editor) {
    markyd_editor_free(self->editor);
  }

  if (self->window) {
    gtk_widget_destroy(self->window);
  }

  g_free(self);
}

void markyd_window_show(MarkydWindow *self) {
  gtk_widget_show(self->window);
  if (!config->window_maximized && config->window_x >= 0 && config->window_y >= 0) {
    gtk_window_move(GTK_WINDOW(self->window), config->window_x, config->window_y);
  }
  gtk_window_present(GTK_WINDOW(self->window));
}

void markyd_window_hide(MarkydWindow *self) { gtk_widget_hide(self->window); }

void markyd_window_toggle(MarkydWindow *self) {
  if (markyd_window_is_visible(self)) {
    markyd_window_hide(self);
  } else {
    markyd_window_show(self);
  }
}

gboolean markyd_window_is_visible(MarkydWindow *self) {
  return gtk_widget_get_visible(self->window);
}

void markyd_window_update_counter(MarkydWindow *self) {
  gchar *text;
  gint count = markyd_app_get_note_count(self->app);
  gint current = self->app->current_index + 1;

  text = g_strdup_printf("%d / %d", current, count);
  gtk_label_set_text(GTK_LABEL(self->lbl_counter), text);
  g_free(text);
}

void markyd_window_update_nav_sensitivity(MarkydWindow *self) {
  gint count = markyd_app_get_note_count(self->app);
  gint current = self->app->current_index;

  gtk_widget_set_sensitive(self->btn_prev, current > 0);
  gtk_widget_set_sensitive(self->btn_next, current < count - 1);
}

static void on_new_clicked(GtkButton *button, gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  (void)button;
  markyd_app_new_note(self->app);
}

static void on_prev_clicked(GtkButton *button, gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  (void)button;
  markyd_app_prev_note(self->app);
}

static void on_next_clicked(GtkButton *button, gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  (void)button;
  markyd_app_next_note(self->app);
}

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event,
                                gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  (void)widget;
  (void)event;

  /* Persist latest geometry when the window is closed-to-tray. */
  if (!config->window_maximized) {
    gint x, y;
    gtk_window_get_position(GTK_WINDOW(self->window), &x, &y);
    config->window_x = x;
    config->window_y = y;
  }
  config_save(config);

  /* Hide instead of destroy */
  markyd_window_hide(self);
  return TRUE;
}

static gboolean on_configure_event(GtkWidget *widget, GdkEventConfigure *event,
                                   gpointer user_data) {
  MarkydWindow *self = (MarkydWindow *)user_data;
  (void)self;

  /* Only save if not maximized (check actual window state to avoid races). */
  GdkWindow *gdk_window = gtk_widget_get_window(widget);
  if (gdk_window) {
    GdkWindowState state = gdk_window_get_state(gdk_window);
    if (state & GDK_WINDOW_STATE_MAXIMIZED) {
      return FALSE;
    }
  } else if (config->window_maximized) {
    return FALSE;
  }

  {
    gint x, y;
    gtk_window_get_position(GTK_WINDOW(widget), &x, &y);
    config->window_x = x;
    config->window_y = y;
    config->window_width = event->width;
    config->window_height = event->height;
  }

  return FALSE;
}

static gboolean on_window_state_event(GtkWidget *widget,
                                      GdkEventWindowState *event,
                                      gpointer user_data) {
  (void)widget;
  (void)user_data;

  config->window_maximized =
      (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;

  return FALSE;
}
