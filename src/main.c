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
  for (int i = 1; i < argc; i++) {
    if (g_strcmp0(argv[i], "--minimized") == 0 ||
        g_strcmp0(argv[i], "-m") == 0) {
      start_minimized = TRUE;
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
  }

  application = markyd_app_new();
  if (!application) {
    g_printerr("Failed to create application\n");
    return 1;
  }

  /* Pass the minimized flag to the app */
  application->start_minimized = start_minimized;
  application->tray_backend = tray_backend;

  status = markyd_app_run(application, argc, argv);

  markyd_app_free(application);

  return status;
}
