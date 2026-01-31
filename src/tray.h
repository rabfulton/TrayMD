#ifndef MARKYD_TRAY_H
#define MARKYD_TRAY_H

#include <gtk/gtk.h>

typedef struct _MarkydApp MarkydApp;

/* Initialize system tray icon */
void tray_init(MarkydApp *app);

/* Cleanup tray */
void tray_cleanup(void);

#endif /* MARKYD_TRAY_H */
