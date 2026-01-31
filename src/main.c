#include "app.h"
#include "tray.h"
#include "window.h"
#include <gtk/gtk.h>
#include <string.h>

static gboolean start_minimized = FALSE;
static MarkydTrayBackend tray_backend = MARKYD_TRAY_BACKEND_STATUSICON;

static gboolean parse_tray_backend(const gchar *value,
                                   MarkydTrayBackend *out) {
  if (!value || !out) {
    return FALSE;
  }

  if (g_ascii_strcasecmp(value, "statusicon") == 0 ||
      g_ascii_strcasecmp(value, "status-icon") == 0 ||
      g_ascii_strcasecmp(value, "status_icon") == 0) {
    *out = MARKYD_TRAY_BACKEND_STATUSICON;
    return TRUE;
  }

  if (g_ascii_strcasecmp(value, "appindicator") == 0 ||
      g_ascii_strcasecmp(value, "app-indicator") == 0 ||
      g_ascii_strcasecmp(value, "app_indicator") == 0) {
    *out = MARKYD_TRAY_BACKEND_APPINDICATOR;
    return TRUE;
  }

  return FALSE;
}

int main(int argc, char **argv) {
  MarkydApp *application;
  int status;
  GPtrArray *filtered;
  int filtered_argc;
  char **filtered_argv;

  /* Read env default */
  const gchar *env_backend = g_getenv("TRAYMD_TRAY_BACKEND");
  if (!env_backend) {
    env_backend = g_getenv("MARKYD_TRAY_BACKEND");
  }
  if (env_backend) {
    MarkydTrayBackend parsed;
    if (parse_tray_backend(env_backend, &parsed)) {
      tray_backend = parsed;
    } else {
      g_printerr("Unknown MARKYD_TRAY_BACKEND '%s' (use statusicon|appindicator)"
                 "\n",
                 env_backend);
    }
  }

  /* Check for --minimized flag before GTK processes args */
  filtered = g_ptr_array_new();
  g_ptr_array_add(filtered, argv[0]);

  for (int i = 1; i < argc; i++) {
    if (g_strcmp0(argv[i], "--minimized") == 0 ||
        g_strcmp0(argv[i], "-m") == 0) {
      start_minimized = TRUE;
      continue;
    }

    if (g_str_has_prefix(argv[i], "--tray-backend=")) {
      const gchar *value = argv[i] + strlen("--tray-backend=");
      MarkydTrayBackend parsed;
      if (parse_tray_backend(value, &parsed)) {
        tray_backend = parsed;
      } else {
        g_printerr("Unknown --tray-backend '%s' (use statusicon|appindicator)\n",
                   value);
      }
      continue;
    }

    if (g_str_has_prefix(argv[i], "--tray=")) {
      const gchar *value = argv[i] + strlen("--tray=");
      MarkydTrayBackend parsed;
      if (parse_tray_backend(value, &parsed)) {
        tray_backend = parsed;
      } else {
        g_printerr("Unknown --tray '%s' (use statusicon|appindicator)\n", value);
      }
      continue;
    }

    if (g_strcmp0(argv[i], "--tray-backend") == 0 && i + 1 < argc) {
      const gchar *value = argv[i + 1];
      MarkydTrayBackend parsed;
      if (parse_tray_backend(value, &parsed)) {
        tray_backend = parsed;
      } else {
        g_printerr("Unknown --tray-backend '%s' (use statusicon|appindicator)\n",
                   value);
      }
      i++;
      continue;
    }

    /* Preserve all other args for GTK/GApplication to parse. */
    g_ptr_array_add(filtered, argv[i]);
  }

  application = markyd_app_new();
  if (!application) {
    g_printerr("Failed to create application\n");
    return 1;
  }

  /* Pass the minimized flag to the app */
  application->start_minimized = start_minimized;
  application->tray_backend = tray_backend;

  filtered_argc = (int)filtered->len;
  filtered_argv = g_new0(char *, (gsize)filtered_argc + 1);
  for (int i = 0; i < filtered_argc; i++) {
    filtered_argv[i] = g_ptr_array_index(filtered, (guint)i);
  }

  status = markyd_app_run(application, filtered_argc, filtered_argv);

  g_free(filtered_argv);
  g_ptr_array_free(filtered, TRUE);

  markyd_app_free(application);

  return status;
}
