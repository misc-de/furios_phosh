/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
 * SPDX-License-Identifier: MIT
 *
 * The plugin, loaded the way phosh loads it (src/plugin-loader.c), and then
 * driven through a state file of its own - written the way the PAM module
 * writes it, through a temporary file and a rename.
 */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <phosh-plugin.h>

#define PLUGIN_NAME "furios-lockout"

static int checks = 0;
static int failures = 0;
static char *state = NULL;


static void
result (const char *what, gboolean good, const char *detail)
{
  checks++;
  if (good) {
    g_print ("  \033[32mok\033[0m   %s\n", what);
  } else {
    failures++;
    g_print ("  \033[31mFAIL\033[0m %s\n       %s\n", what, detail ? detail : "");
  }
}


/* As the module does it: a temp file, then rename. */
static void
write_state (const char *text)
{
  g_autofree char *tmp = g_strconcat (state, ".tmp", NULL);

  if (text == NULL) {
    g_remove (state);
    return;
  }
  if (!g_file_set_contents (tmp, text, -1, NULL) || g_rename (tmp, state) != 0)
    g_error ("could not write %s", state);
}


static void
lock_for (gint64 start_offset, gint64 length)
{
  gint64 now = g_get_real_time () / G_USEC_PER_SEC;
  g_autofree char *text = g_strdup_printf ("0 1 %" G_GINT64_FORMAT " %"
                                           G_GINT64_FORMAT " %" G_GINT64_FORMAT "\n",
                                           now + start_offset, length,
                                           now + start_offset);
  write_state (text);
}


static GtkWidget *
label_of (GtkWidget *widget)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
  GtkWidget *label = g_list_nth_data (children, 1);

  g_list_free (children);
  return label;
}


/* Pump the main loop until the widget shows what it should, or give up
   after `seconds`. The file monitor is what answers here - well inside the
   60 s safety poll, so a monitor that never fires fails this test. */
static gboolean
settles (GtkWidget *widget, gboolean visible, const char *prefix, int seconds)
{
  gint64 deadline = g_get_monotonic_time () + seconds * G_USEC_PER_SEC;

  while (g_get_monotonic_time () < deadline) {
    const char *text = gtk_label_get_text (GTK_LABEL (label_of (widget)));

    if (gtk_widget_get_visible (widget) == visible &&
        (prefix == NULL || g_str_has_prefix (text, prefix)))
      return TRUE;
    g_main_context_iteration (NULL, FALSE);
    g_usleep (10 * 1000);
  }
  return FALSE;
}


static const char *shown (GtkWidget *widget);

/* TRUE once the label reads anything other than `before`. */
static gboolean
changes_within (GtkWidget *widget, const char *before, int seconds)
{
  gint64 deadline = g_get_monotonic_time () + seconds * G_USEC_PER_SEC;

  while (g_get_monotonic_time () < deadline) {
    if (g_strcmp0 (before, shown (widget)) != 0)
      return TRUE;
    g_main_context_iteration (NULL, FALSE);
    g_usleep (10 * 1000);
  }
  return FALSE;
}


static const char *
shown (GtkWidget *widget)
{
  return gtk_label_get_text (GTK_LABEL (label_of (widget)));
}


int
main (int argc, char *argv[])
{
  GIOExtensionPoint *ep;
  GIOExtension *extension;
  GtkWidget *widget;
  g_autofree char *dir = NULL;
  char before[32];

  if (argc < 2) {
    g_printerr ("usage: %s <directory holding the built plugin>\n", argv[0]);
    return 2;
  }

  dir = g_dir_make_tmp ("lockout-status-test-XXXXXX", NULL);
  state = g_build_filename (dir, "state", NULL);
  g_setenv ("FURIOS_LOCKOUT_STATE", state, TRUE);

  if (!gtk_init_check (&argc, &argv)) {
    g_print ("  \033[33mskipped\033[0m - no display to build a GTK widget on\n");
    return 77;
  }

  ep = g_io_extension_point_register (PHOSH_PLUGIN_EXTENSION_POINT_STATUS_ICON_WIDGET);
  g_io_extension_point_set_required_type (ep, GTK_TYPE_WIDGET);
  g_io_modules_scan_all_in_directory (argv[1]);

  extension = g_io_extension_point_get_extension_by_name (ep, PLUGIN_NAME);
  result ("the shell finds it under the name the settings hold",
          extension != NULL, "no extension '" PLUGIN_NAME "'");
  if (extension == NULL)
    return 1;

  widget = g_object_new (g_io_extension_get_type (extension), NULL);
  g_object_ref_sink (widget);
  /* phosh calls show_all on what it is handed. */
  gtk_widget_show_all (widget);

  result ("no state file: nothing in the bar", !gtk_widget_get_visible (widget), NULL);

  write_state ("2 0 0 0 1790000000\n");
  result ("failures but no lock: nothing in the bar",
          settles (widget, FALSE, NULL, 3), NULL);

  lock_for (-28, 300);
  result ("a lock appears within seconds, time left 4:3x",
          settles (widget, TRUE, "4:3", 3), shown (widget));

  g_strlcpy (before, shown (widget), sizeof before);
  result ("and counts down, a second at a time",
          changes_within (widget, before, 3), shown (widget));

  lock_for (0, 2 * 3600);
  result ("a long lock shows hours",
          settles (widget, TRUE, "1:59:5", 3) || settles (widget, TRUE, "2:00:00", 1),
          shown (widget));

  lock_for (-301, 300);
  result ("a lock that has run out goes away",
          settles (widget, FALSE, NULL, 3), NULL);

  lock_for (86400, 300);
  result ("a clock that went back shows the full length, not a day",
          settles (widget, TRUE, "5:00", 3), shown (widget));

  write_state ("rubbish\n");
  result ("a garbled file: nothing in the bar", settles (widget, FALSE, NULL, 3), NULL);

  lock_for (0, 300);
  settles (widget, TRUE, NULL, 3);
  write_state (NULL);
  result ("the file removed (secctl unlock): gone at once",
          settles (widget, FALSE, NULL, 3), NULL);

  g_object_unref (widget);
  g_remove (state);
  g_rmdir (dir);

  g_print ("\n  %d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
