/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
 * SPDX-License-Identifier: MIT
 *
 * A lock and the time left, in the top bar, while the lock screen refuses
 * PINs after failed attempts.
 *
 * The lockout itself is pam_furios_lockout (furios_security, "secctl apply
 * lockout"). It can tell the lock screen nothing: phosh drops every PAM
 * message ("TBD" in its conversation handler), so during a lock the right
 * PIN is refused without a word. The top bar is visible on the PIN page as
 * well, and that is where this says why.
 *
 * It reads the module's state file - five numbers, written by the module in
 * this very process as this user - and shows nothing unless it holds a lock
 * that has not run out. A file monitor brings the change the moment the
 * module writes it; a second timer ticks the countdown only while a lock is
 * on, so an unlocked phone costs nothing. A slow poll stands behind the
 * monitor in case it never fires (a directory that did not exist yet when
 * the shell started, say).
 *
 * Every failure is "show nothing": unreadable, garbled, too long - no icon.
 * The lock is the module's business; this only reports it.
 */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <phosh-plugin.h>

#define PLUGIN_NAME "furios-lockout"

/* The module's state file, relative to the home. The environment variable is
   how the test points this at a file of its own. */
#define STATE_REL "furios-lockout/state"
#define STATE_ENV "FURIOS_LOCKOUT_STATE"

/* "pending strikes lock_start lock_len last" - five numbers, well under
   this. Anything longer is not the module's file. */
#define MAX_LEN 128

#define SAFETY_POLL_SECONDS 60

#define ICON_SIZE 16
#define ICON_SPACING 4

/* Amber like the kill switch icons: something is refused, look here. */
#define CSS                                               \
  "image, label {"                                        \
  "  color: #ffb648;"                                     \
  "}"                                                     \
  "image { -gtk-icon-shadow: 0 1px 2px rgba(0,0,0,0.6); }" \
  "label { text-shadow: 0 1px 2px rgba(0,0,0,0.6);"       \
  "        font-feature-settings: \"tnum\"; }"


#define FURIOS_TYPE_LOCKOUT_STATUS (furios_lockout_status_get_type ())
G_DECLARE_FINAL_TYPE (FuriosLockoutStatus, furios_lockout_status, FURIOS,
                      LOCKOUT_STATUS, GtkBox)

struct _FuriosLockoutStatus {
  GtkBox        parent_instance;

  GtkWidget    *image;
  GtkWidget    *label;
  char         *path;
  GFileMonitor *monitor;
  guint         tick_id;
  guint         poll_id;
};

G_DEFINE_TYPE (FuriosLockoutStatus, furios_lockout_status, GTK_TYPE_BOX)


/*
 * Seconds left in the lock, or 0 for none - including every file that is
 * not plainly the module's.
 *
 * The same reading as the module's own: a lock whose start lies in the
 * future means the clock went back, and the module then counts the full
 * length from now. Shown the same way, so the bar never promises less than
 * the lock screen will hold.
 */
static gint64
seconds_left (const char *path)
{
  g_autofree char *text = NULL;
  gsize len = 0;
  gint64 v[5];
  char *p, *end;
  gint64 now, until;

  if (!g_file_get_contents (path, &text, &len, NULL))
    return 0;
  if (len == 0 || len > MAX_LEN)
    return 0;

  p = text;
  for (int i = 0; i < 5; i++) {
    v[i] = g_ascii_strtoll (p, &end, 10);
    if (end == p || v[i] < 0)
      return 0;
    p = end;
  }

  /* v[2] lock_start, v[3] lock_len */
  if (v[3] <= 0)
    return 0;
  now = g_get_real_time () / G_USEC_PER_SEC;
  if (now < v[2])
    return v[3];
  until = v[2] + v[3];
  return until > now ? until - now : 0;
}


static gboolean on_tick (gpointer user_data);

/* The children as well as the box: phosh calls gtk_widget_show() on the box
   it is handed, which no_show_all does not stop, and a shown box with a
   shown lock in it is a lock in the bar. Hidden children leave it empty. */
static void
set_showing (FuriosLockoutStatus *self, gboolean on)
{
  gtk_widget_set_visible (self->image, on);
  gtk_widget_set_visible (self->label, on);
  gtk_widget_set_visible (GTK_WIDGET (self), on);
}

static void
refresh (FuriosLockoutStatus *self)
{
  gint64 left = seconds_left (self->path);

  if (left <= 0) {
    g_clear_handle_id (&self->tick_id, g_source_remove);
    set_showing (self, FALSE);
    return;
  }

  {
    g_autofree char *text = NULL;

    /* Hours only once there are any: 480 minutes is a lock that exists. */
    if (left >= 3600)
      text = g_strdup_printf ("%d:%02d:%02d", (int) (left / 3600),
                              (int) (left / 60 % 60), (int) (left % 60));
    else
      text = g_strdup_printf ("%d:%02d", (int) (left / 60), (int) (left % 60));
    gtk_label_set_text (GTK_LABEL (self->label), text);
  }
  set_showing (self, TRUE);

  if (self->tick_id == 0)
    self->tick_id = g_timeout_add_seconds (1, on_tick, self);
}


static gboolean
on_tick (gpointer user_data)
{
  FuriosLockoutStatus *self = user_data;

  refresh (self);
  /* refresh() removed the source itself when the lock ran out. */
  return self->tick_id ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}


static gboolean
on_poll (gpointer user_data)
{
  refresh (user_data);
  return G_SOURCE_CONTINUE;
}


static void
on_changed (GFileMonitor      *monitor,
            GFile             *file,
            GFile             *other,
            GFileMonitorEvent  event,
            gpointer           user_data)
{
  refresh (user_data);
}


static void
furios_lockout_status_dispose (GObject *object)
{
  FuriosLockoutStatus *self = FURIOS_LOCKOUT_STATUS (object);

  g_clear_handle_id (&self->tick_id, g_source_remove);
  g_clear_handle_id (&self->poll_id, g_source_remove);
  if (self->monitor) {
    g_signal_handlers_disconnect_by_data (self->monitor, self);
    g_file_monitor_cancel (self->monitor);
    g_clear_object (&self->monitor);
  }

  G_OBJECT_CLASS (furios_lockout_status_parent_class)->dispose (object);
}


static void
furios_lockout_status_finalize (GObject *object)
{
  FuriosLockoutStatus *self = FURIOS_LOCKOUT_STATUS (object);

  g_clear_pointer (&self->path, g_free);

  G_OBJECT_CLASS (furios_lockout_status_parent_class)->finalize (object);
}


static void
furios_lockout_status_class_init (FuriosLockoutStatusClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = furios_lockout_status_dispose;
  object_class->finalize = furios_lockout_status_finalize;
}


static void
furios_lockout_status_init (FuriosLockoutStatus *self)
{
  g_autoptr (GtkCssProvider) provider = gtk_css_provider_new ();
  g_autoptr (GFile) file = NULL;
  const char *env = g_getenv (STATE_ENV);

  self->path = (env && *env) ? g_strdup (env)
    : g_build_filename (g_get_user_state_dir (), STATE_REL, NULL);

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self),
                                  GTK_ORIENTATION_HORIZONTAL);
  gtk_box_set_spacing (GTK_BOX (self), ICON_SPACING);
  gtk_widget_set_valign (GTK_WIDGET (self), GTK_ALIGN_CENTER);
  gtk_css_provider_load_from_data (provider, CSS, -1, NULL);

  self->image = gtk_image_new_from_icon_name ("system-lock-screen-symbolic",
                                              GTK_ICON_SIZE_MENU);
  gtk_image_set_pixel_size (GTK_IMAGE (self->image), ICON_SIZE);
  self->label = gtk_label_new ("");
  /* On these two widgets alone - a plugin's stylesheet must not reach the
     rest of the shell. */
  gtk_style_context_add_provider (gtk_widget_get_style_context (self->image),
                                  GTK_STYLE_PROVIDER (provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_style_context_add_provider (gtk_widget_get_style_context (self->label),
                                  GTK_STYLE_PROVIDER (provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_box_pack_start (GTK_BOX (self), self->image, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self), self->label, FALSE, FALSE, 0);
  /* phosh shows every widget it is handed; staying away is our doing -
     refresh() below decides for all three. */
  gtk_widget_set_no_show_all (self->image, TRUE);
  gtk_widget_set_no_show_all (self->label, TRUE);
  gtk_widget_set_no_show_all (GTK_WIDGET (self), TRUE);

  file = g_file_new_for_path (self->path);
  self->monitor = g_file_monitor_file (file, G_FILE_MONITOR_WATCH_MOVES,
                                       NULL, NULL);
  if (self->monitor)
    g_signal_connect (self->monitor, "changed", G_CALLBACK (on_changed), self);
  self->poll_id = g_timeout_add_seconds (SAFETY_POLL_SECONDS, on_poll, self);

  refresh (self);
}


/* --- the GIO module, which is how phosh finds any of this ---------------- */

void
g_io_module_load (GIOModule *module)
{
  g_type_module_use (G_TYPE_MODULE (module));

  g_io_extension_point_implement (PHOSH_PLUGIN_EXTENSION_POINT_STATUS_ICON_WIDGET,
                                  FURIOS_TYPE_LOCKOUT_STATUS,
                                  PLUGIN_NAME,
                                  10);
}


void
g_io_module_unload (GIOModule *module)
{
}


char **
g_io_module_query (void)
{
  char *points[] = { (char *) PHOSH_PLUGIN_EXTENSION_POINT_STATUS_ICON_WIDGET,
                     NULL };

  return g_strdupv (points);
}
