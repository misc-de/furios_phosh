/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
 * SPDX-License-Identifier: MIT
 *
 * The plugin, loaded the way phosh loads it, against the app grid's shape.
 *
 * The plugin looks for PhoshAppGrid and PhoshAppGridFolderButton by name and
 * for phosh's "apps" and "scrolled_window" by their ids in app-grid.ui. None
 * of those is in a header we have, so the test builds that shape itself -
 * the same nesting, the same ids, a flowbox bound to a model the way phosh
 * binds its own - and checks what the plugin does with it: folders go to the
 * dock and still open, the dock follows the model, and everything is back
 * where it was when the widget goes.
 */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <dlfcn.h>
#include <glib/gstdio.h>
#include <phosh-plugin.h>

#define PLUGIN_NAME "furios-folder-dock"

static int checks = 0;
static int failures = 0;
static int opened = 0;
static GType folder_type;


static void
check (const char *what, gboolean value)
{
  checks++;
  if (value) {
    g_print ("  \033[32mok\033[0m   %s\n", what);
  } else {
    failures++;
    g_print ("  \033[31mFAIL\033[0m %s\n", what);
  }
}


static GType
load_phosh_status_icon_type (void)
{
  static const char *sonames[] = {
    "libphosh-0.45.so.0", "libphosh-0.46.so.0", "libphosh-0.47.so.0", NULL
  };
  GType (*get_type) (void);
  void *lib = NULL;

  for (int i = 0; sonames[i] && lib == NULL; i++)
    lib = dlopen (sonames[i], RTLD_NOW | RTLD_GLOBAL);
  if (lib == NULL)
    return 0;
  get_type = dlsym (lib, "phosh_status_icon_get_type");
  return get_type ? get_type () : 0;
}


static GType
stand_in_type (const char *name, GType parent)
{
  GTypeQuery query = { 0 };
  GTypeInfo info = { 0 };

  g_type_query (parent, &query);
  info.class_size = query.class_size;
  info.instance_size = query.instance_size;
  return g_type_register_static (parent, name, &info, 0);
}


static void
settle (void)
{
  gint64 until = g_get_monotonic_time () + 300 * 1000;

  while (g_get_monotonic_time () < until)
    g_main_context_iteration (NULL, FALSE);
}


static void
on_folder_clicked (GtkButton *button, gpointer user_data)
{
  opened++;
}


/* What phosh's create-widget function does, reduced to the one difference
   that matters here: a folder item becomes a folder button, with the
   connection that opens it made right here, before the plugin sees it. */
static GtkWidget *
create_child (gpointer item, gpointer user_data)
{
  const char *name = g_object_get_data (item, "name");
  GtkWidget *button;

  if (g_object_get_data (item, "folder")) {
    button = g_object_new (folder_type, "label", name, NULL);
    g_signal_connect (button, "clicked", G_CALLBACK (on_folder_clicked), NULL);
  } else {
    button = gtk_button_new_with_label (name);
  }
  gtk_widget_show (button);
  return button;
}


static GObject *
item (const char *name, gboolean folder)
{
  GObject *obj = g_object_new (G_TYPE_OBJECT, NULL);

  g_object_set_data_full (obj, "name", g_strdup (name), g_free);
  g_object_set_data (obj, "folder", GINT_TO_POINTER (folder));
  return obj;
}


static void
store_append (GListStore *store, const char *name, gboolean folder)
{
  GObject *obj = item (name, folder);

  g_list_store_append (store, obj);
  g_object_unref (obj);
}


static int
count_children (GtkWidget *flow, gboolean folders_only, gboolean visible_only)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (flow));
  int n = 0;

  for (GList *l = children; l; l = l->next) {
    GtkWidget *inner = gtk_bin_get_child (GTK_BIN (l->data));

    if (visible_only && !gtk_widget_get_visible (l->data))
      continue;
    if (folders_only && !(inner && G_TYPE_CHECK_INSTANCE_TYPE (inner, folder_type)))
      continue;
    n++;
  }
  g_list_free (children);
  return n;
}


static void
count_docks_cb (GtkWidget *widget, gpointer user_data)
{
  GPtrArray *found = user_data;

  if (gtk_style_context_has_class (gtk_widget_get_style_context (widget), PLUGIN_NAME))
    g_ptr_array_add (found, widget);
  if (GTK_IS_CONTAINER (widget))
    gtk_container_forall (GTK_CONTAINER (widget), count_docks_cb, found);
}


static GPtrArray *
docks_in (GtkWidget *root)
{
  GPtrArray *found = g_ptr_array_new ();

  gtk_container_forall (GTK_CONTAINER (root), count_docks_cb, found);
  return found;
}


static GtkWidget *
find_dock (GtkWidget *column)
{
  GPtrArray *found = docks_in (column);
  GtkWidget *dock = found->len ? g_ptr_array_index (found, 0) : NULL;

  g_ptr_array_free (found, TRUE);
  return dock;
}


static int
position_in (GtkWidget *box, GtkWidget *child)
{
  int pos = -1;

  gtk_container_child_get (GTK_CONTAINER (box), child, "position", &pos, NULL);
  return pos;
}


/* The flowbox inside the dock: the only child of its vertical box. */
static GtkWidget *
dock_flow (GtkWidget *dock)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (dock));
  GtkWidget *last = g_list_last (children)->data;

  g_list_free (children);
  return last;
}


static GtkWidget *
first_folder_in (GtkWidget *flow)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (flow));
  GtkWidget *found = NULL;

  for (GList *l = children; l && !found; l = l->next) {
    GtkWidget *inner = gtk_bin_get_child (GTK_BIN (l->data));

    if (inner && G_TYPE_CHECK_INSTANCE_TYPE (inner, folder_type))
      found = inner;
  }
  g_list_free (children);
  return found;
}


int
main (int argc, char *argv[])
{
  GType status_icon_type, grid_type, type;
  GIOExtensionPoint *ep;
  GIOExtension *extension;
  GtkWidget *window, *grid, *column, *search, *scrolled, *inner, *apps;
  GtkWidget *one, *two, *dock;
  GListStore *store;
  char *guard;

  if (argc < 2) {
    g_printerr ("usage: %s <directory holding the built plugin>\n", argv[0]);
    return 2;
  }
  /* The crash mark goes to the cache directory; not the real one. GLib
     remembers the answer, so this comes before anything asks. */
  {
    char *cache = g_dir_make_tmp ("folder-dock-test-XXXXXX", NULL);

    g_setenv ("XDG_CACHE_HOME", cache, TRUE);
    guard = g_build_filename (cache, "furios-folder-dock.armed", NULL);
    g_free (cache);
  }
  if (!gtk_init_check (&argc, &argv)) {
    g_print ("  \033[33mskipped\033[0m - no display to build a GTK widget on\n");
    return 77;
  }
  status_icon_type = load_phosh_status_icon_type ();
  if (status_icon_type == 0) {
    g_print ("  \033[33mskipped\033[0m - no libphosh to be a status icon in\n");
    return 77;
  }

  grid_type = stand_in_type ("PhoshAppGrid", GTK_TYPE_BOX);
  folder_type = stand_in_type ("PhoshAppGridFolderButton", GTK_TYPE_BUTTON);

  ep = g_io_extension_point_register (PHOSH_PLUGIN_EXTENSION_POINT_STATUS_ICON_WIDGET);
  g_io_extension_point_set_required_type (ep, GTK_TYPE_WIDGET);
  g_io_modules_scan_all_in_directory (argv[1]);
  extension = g_io_extension_point_get_extension_by_name (ep, PLUGIN_NAME);
  check ("the shell finds it under the name the settings hold", extension != NULL);
  if (!extension)
    return 1;
  type = g_io_extension_get_type (extension);
  check ("a status icon, so the bar can hold it", g_type_is_a (type, status_icon_type));

  /* app-grid.ui, as far as the plugin reads it. */
  window = gtk_offscreen_window_new ();
  gtk_window_set_default_size (GTK_WINDOW (window), 360, 720);
  grid = g_object_new (grid_type, NULL);
  column = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  search = gtk_search_entry_new ();
  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_buildable_set_name (GTK_BUILDABLE (scrolled), "scrolled_window");
  gtk_widget_set_vexpand (scrolled, TRUE);
  inner = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  apps = gtk_flow_box_new ();
  gtk_buildable_set_name (GTK_BUILDABLE (apps), "apps");
  gtk_container_add (GTK_CONTAINER (inner), apps);
  gtk_container_add (GTK_CONTAINER (scrolled), inner);
  gtk_container_add (GTK_CONTAINER (column), search);
  gtk_box_pack_start (GTK_BOX (column), scrolled, TRUE, TRUE, 0);
  gtk_container_add (GTK_CONTAINER (grid), column);
  gtk_container_add (GTK_CONTAINER (window), grid);

  store = g_list_store_new (G_TYPE_OBJECT);
  store_append (store, "Office", TRUE);
  store_append (store, "Calls", FALSE);
  store_append (store, "Games", TRUE);
  store_append (store, "Maps", FALSE);
  gtk_flow_box_bind_model (GTK_FLOW_BOX (apps), G_LIST_MODEL (store),
                           create_child, NULL, NULL);
  gtk_widget_show_all (window);
  settle ();

  /* Second run, own process: the mark of a shell that did not survive is
     already there when the first instance is built. */
  if (g_getenv ("FOLDER_DOCK_LEFTOVER_MARK")) {
    g_file_set_contents (guard, "", 0, NULL);
    one = g_object_new (type, NULL);
    g_object_ref_sink (one);
    settle ();
    check ("a mark left by a crash keeps the grid untouched",
           find_dock (column) == NULL && count_children (apps, TRUE, TRUE) == 2);
    check ("and the mark stays until somebody switches it on again",
           g_file_test (guard, G_FILE_TEST_EXISTS));
    gtk_widget_destroy (one);
    settle ();
    check ("switching off then changes nothing either", count_children (apps, TRUE, TRUE) == 2);
    /* What the app does when the switch goes on again. */
    g_unlink (guard);
    one = g_object_new (type, NULL);
    g_object_ref_sink (one);
    settle ();
    check ("with the mark gone, switching on works in the same shell",
           find_dock (column) != NULL);
    gtk_widget_destroy (one);
    settle ();
    goto done;
  }

  one = g_object_new (type, NULL);
  g_object_ref_sink (one);
  settle ();

  check ("nothing of it shows in the bar", !gtk_widget_get_visible (one));
  check ("the crash mark is set while the dock is young",
         g_file_test (guard, G_FILE_TEST_EXISTS));
  dock = find_dock (column);
  check ("a dock under the scrolled area", dock != NULL);
  if (!dock)
    return 1;
  {
    GtkWidget *overlay = gtk_widget_get_parent (dock);
    gboolean expand = FALSE;

    check ("it lies in an overlay over the scrolled area",
           GTK_IS_OVERLAY (overlay) && gtk_bin_get_child (GTK_BIN (overlay)) == scrolled);
    check ("the overlay takes the scrolled window's place in the column",
           gtk_widget_get_parent (overlay) == column && position_in (column, overlay) == 1);
    gtk_box_query_child_packing (GTK_BOX (column), overlay, &expand, NULL, NULL, NULL);
    check ("and its packing: all the height there is", expand);
    check ("the dock hangs at the bottom edge", gtk_widget_get_valign (dock) == GTK_ALIGN_END);
    check ("the apps get room at the end as tall as the dock, to scroll out from under it",
           gtk_widget_get_allocated_height (dock) > 0 &&
           gtk_widget_get_margin_bottom (inner) == gtk_widget_get_allocated_height (dock));
  }
  check ("both folders are in it", count_children (dock_flow (dock), TRUE, FALSE) == 2);
  check ("no folder is left visible in the grid", count_children (apps, TRUE, TRUE) == 0);
  check ("the apps stay where they were", count_children (apps, FALSE, TRUE) == 2);

  opened = 0;
  gtk_button_clicked (GTK_BUTTON (first_folder_in (dock_flow (dock))));
  check ("a moved folder still opens through phosh's own connection", opened == 1);

  /* phosh drops a folder: the model loses it, the flowbox destroys its
     child - and the button we hold has to go with it. */
  g_list_store_remove (store, 0);
  settle ();
  check ("a folder phosh removed leaves the dock too",
         count_children (dock_flow (dock), TRUE, FALSE) == 1);

  store_append (store, "Tools", TRUE);
  settle ();
  check ("a new folder is picked up", count_children (dock_flow (dock), TRUE, FALSE) == 2);
  check ("and it is not left in the grid as well", count_children (apps, TRUE, TRUE) == 0);

  /* A second bar with status icons: one dock, not two. */
  two = g_object_new (type, NULL);
  g_object_ref_sink (two);
  settle ();
  {
    GPtrArray *docks = docks_in (column);

    check ("a second instance does not build a second dock", docks->len == 1);
    g_ptr_array_free (docks, TRUE);
  }

  gtk_widget_destroy (one);
  settle ();
  check ("one instance gone, the other still holds the dock", find_dock (column) != NULL);

  gtk_widget_destroy (two);
  settle ();
  check ("the last one gone takes the dock with it", find_dock (column) == NULL);
  {
    gboolean expand = FALSE;

    check ("the scrolled window is back in the column, at its place",
           gtk_widget_get_parent (scrolled) == column && position_in (column, scrolled) == 1);
    gtk_box_query_child_packing (GTK_BOX (column), scrolled, &expand, NULL, NULL, NULL);
    check ("with its packing", expand);
    check ("no overlay is left behind",
           g_list_length (gtk_container_get_children (GTK_CONTAINER (column))) == 2);
    check ("and the room at the end of the apps is gone", gtk_widget_get_margin_bottom (inner) == 0);
  }
  check ("and a dock taken down in good order clears the crash mark",
         !g_file_test (guard, G_FILE_TEST_EXISTS));
  check ("every folder is back in the grid", count_children (apps, TRUE, TRUE) == 2);
  check ("and nothing else changed there", count_children (apps, FALSE, TRUE) == 4);

  opened = 0;
  gtk_button_clicked (GTK_BUTTON (first_folder_in (apps)));
  check ("a folder put back still opens", opened == 1);

  /* On again after off, as when the switch is flipped twice. */
  one = g_object_new (type, NULL);
  g_object_ref_sink (one);
  settle ();
  dock = find_dock (column);
  check ("switched on again, the dock comes back",
         dock && count_children (dock_flow (dock), TRUE, FALSE) == 2);
  gtk_widget_destroy (one);
  settle ();
  check ("and goes again", find_dock (column) == NULL);

done:
  gtk_widget_destroy (window);

  if (failures) {
    g_print ("\n\033[31m%d of %d checks failed\033[0m\n", failures, checks);
    return 1;
  }
  g_print ("\n\033[32mall %d checks passed\033[0m\n", checks);
  return 0;
}
