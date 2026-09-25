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
 * binds its own - and checks what the plugin does with it: folders show in
 * the dock and still open, the dock follows the model, and everything is back
 * where it was when the widget goes.
 *
 * The folder button is built the way phosh 0.55 builds it, because the first
 * version of this test did not and passed on a plugin that on the phone
 * never found a single folder: PhoshAppGridFolderButton IS the flowbox child
 * (it derives from GtkFlowBoxChild through PhoshAppGridBaseButton), it has a
 * "folder-info" property, and it opens its folder by emitting
 * "folder-launched", which the grid connects when it makes the button. And
 * the flowbox is bound to a model, so GTK drops children BY INDEX - whatever
 * the plugin does must leave every index pointing at its own item.
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


/* --- PhoshAppGridFolderButton, as far as its shape goes ------------------- */

enum { PROP_0, PROP_FOLDER_INFO };
static guint folder_launched_signal;
static GObject *last_opened;


static void
folder_set_property (GObject *object, guint id, const GValue *value, GParamSpec *pspec)
{
  if (id == PROP_FOLDER_INFO)
    g_object_set_data_full (object, "info", g_value_dup_object (value), g_object_unref);
}


static void
folder_get_property (GObject *object, guint id, GValue *value, GParamSpec *pspec)
{
  if (id == PROP_FOLDER_INFO)
    g_value_set_object (value, g_object_get_data (object, "info"));
}


static void
folder_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = folder_set_property;
  object_class->get_property = folder_get_property;
  g_object_class_install_property (object_class, PROP_FOLDER_INFO,
    g_param_spec_object ("folder-info", "", "", G_TYPE_OBJECT,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  folder_launched_signal = g_signal_new ("folder-launched", G_TYPE_FROM_CLASS (klass),
                                         G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                         G_TYPE_NONE, 1, G_TYPE_OBJECT);
}


/* The button inside, as in the template: pressing it is "folder-launched". */
static void
on_inner_clicked (GtkButton *inner, GtkWidget *folder)
{
  g_signal_emit (folder, folder_launched_signal, 0, g_object_get_data (G_OBJECT (folder), "info"));
}


static void
folder_init (GTypeInstance *instance, gpointer klass)
{
  GtkWidget *inner = gtk_button_new ();

  g_signal_connect (inner, "clicked", G_CALLBACK (on_inner_clicked), instance);
  gtk_container_add (GTK_CONTAINER (instance), inner);
  gtk_widget_show (inner);
}


static GType
folder_button_type (void)
{
  GTypeQuery query = { 0 };
  GTypeInfo info = { 0 };

  g_type_query (GTK_TYPE_FLOW_BOX_CHILD, &query);
  info.class_size = query.class_size;
  info.class_init = folder_class_init;
  info.instance_size = query.instance_size;
  info.instance_init = folder_init;
  return g_type_register_static (GTK_TYPE_FLOW_BOX_CHILD, "PhoshAppGridFolderButton", &info, 0);
}


/* What phosh's grid connects in create_launcher: open the folder. */
static void
on_folder_launched (GtkWidget *button, GObject *info, gpointer user_data)
{
  opened++;
  last_opened = info;
}


static void
press (GtkWidget *folder)
{
  gtk_button_clicked (GTK_BUTTON (gtk_bin_get_child (GTK_BIN (folder))));
}


/* phosh's create_launcher: a folder item becomes a folder button - the
   flowbox child itself - with the connection that opens it made right here,
   before the plugin sees it; an app becomes an app button. */
static GtkWidget *
create_child (gpointer item, gpointer user_data)
{
  GtkWidget *child;

  if (g_object_get_data (item, "folder")) {
    child = g_object_new (folder_type, "folder-info", item, NULL);
    g_signal_connect (child, "folder-launched", G_CALLBACK (on_folder_launched), NULL);
  } else {
    child = gtk_flow_box_child_new ();
    gtk_container_add (GTK_CONTAINER (child),
                       gtk_button_new_with_label (g_object_get_data (item, "name")));
  }
  g_object_set_data (G_OBJECT (child), "item", item);
  gtk_widget_show_all (child);
  return child;
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
    if (visible_only && !gtk_widget_get_visible (l->data))
      continue;
    if (folders_only && !G_TYPE_CHECK_INSTANCE_TYPE (l->data, folder_type))
      continue;
    n++;
  }
  g_list_free (children);
  return n;
}


/* Every child of phosh's flowbox at the index of its own item: what GTK's
   bound model relies on when it drops a child by index. */
static gboolean
grid_matches_model (GtkWidget *apps, GListModel *model)
{
  guint n = g_list_model_get_n_items (model);

  for (guint i = 0; i < n; i++) {
    GtkFlowBoxChild *child = gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (apps), i);
    GObject *item = g_list_model_get_item (model, i);
    gboolean same = child && g_object_get_data (G_OBJECT (child), "item") == item;

    g_object_unref (item);
    if (!same)
      return FALSE;
  }
  return gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (apps), n) == NULL;
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
    if (G_TYPE_CHECK_INSTANCE_TYPE (l->data, folder_type))
      found = l->data;
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
  folder_type = folder_button_type ();

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
  check ("nothing is taken out of phosh's flowbox: every index still its own item",
         grid_matches_model (apps, G_LIST_MODEL (store)));

  opened = 0;
  last_opened = NULL;
  press (first_folder_in (dock_flow (dock)));
  {
    GObject *office = g_list_model_get_item (G_LIST_MODEL (store), 0);

    check ("a folder in the dock opens through phosh's own connection",
           opened == 1 && last_opened == office);
    g_object_unref (office);
  }

  /* phosh drops a folder: the model loses it, the flowbox destroys the
     child at that index - which has to be the right one. */
  g_list_store_remove (store, 0);
  settle ();
  check ("phosh drops the right child when a folder goes",
         grid_matches_model (apps, G_LIST_MODEL (store)));
  check ("and it leaves the dock too",
         count_children (dock_flow (dock), TRUE, FALSE) == 1);

  store_append (store, "Tools", TRUE);
  settle ();
  check ("a new folder is picked up", count_children (dock_flow (dock), TRUE, FALSE) == 2);
  check ("and it is not left in view in the grid as well", count_children (apps, TRUE, TRUE) == 0);
  check ("and the grid still matches the model", grid_matches_model (apps, G_LIST_MODEL (store)));

  /* An app between them goes: indices shift under the hidden folders. */
  g_list_store_remove (store, 0);
  settle ();
  check ("an app removed in front of the folders takes only itself",
         grid_matches_model (apps, G_LIST_MODEL (store)) &&
         count_children (dock_flow (dock), TRUE, FALSE) == 2);

  /* phosh showing its whole grid again does not bring the folders back. */
  gtk_widget_show_all (grid);
  settle ();
  check ("a show_all on the grid leaves the folders hidden",
         count_children (apps, TRUE, TRUE) == 0);

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
  check ("every folder is back in view in the grid", count_children (apps, TRUE, TRUE) == 2);
  check ("and nothing else changed there", count_children (apps, FALSE, TRUE) == 3 &&
         grid_matches_model (apps, G_LIST_MODEL (store)));

  opened = 0;
  press (first_folder_in (apps));
  check ("a folder in the grid still opens", opened == 1);

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
