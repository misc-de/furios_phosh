/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
 * SPDX-License-Identifier: MIT
 *
 * The folders of phosh's app overview, held in a bar at the bottom of the
 * screen instead of scrolling away with the apps - lying over them, so the
 * apps pass underneath it.
 *
 * phosh has no place for this. Folders and apps share one GtkFlowBox inside
 * the scrolled area, GTK 3's CSS can neither reorder nor pin anything, and
 * no plugin type reaches the app grid. So this is a status icon that shows
 * nothing, and the reason it exists is a side effect: while it is loaded it
 * finds the app grid in the shell's own windows and rearranges it.
 *
 * It does NOT move phosh's folder buttons. In phosh 0.55 a folder button IS
 * the GtkFlowBoxChild (PhoshAppGridBaseButton derives from it), and the
 * flowbox is bound to a list model: when the model changes, GTK finds the
 * children to drop BY INDEX. A button taken out would shift every index
 * after it, and phosh would destroy the wrong launchers. So the originals
 * stay exactly where they are, only hidden, and the dock holds copies of the
 * same type built from the same folder info. A copy opens its folder by
 * emitting "folder-launched" on its original - the signal phosh's grid
 * connected when it made that button, so the folder opens exactly as if the
 * original had been pressed.
 *
 * Switching it on and off is the `status-icons` list, which phosh follows
 * while it runs: taking the name out destroys our widget, and on destroy
 * every button goes back into the child it came from. The shell is left as
 * it was found.
 *
 * This runs in phosh's process and depends on the inside of phosh's app
 * grid, which is not an interface. Every lookup is by name and checked; a
 * shape that is not the one described here means "do nothing", never a
 * guess. Checked against phosh 0.55.
 */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <phosh-plugin.h>
#include <glib/gstdio.h>
#include <string.h>

/* The one symbol borrowed from the shell - see phosh-battery-time for why
   it is declared here and not included. */
GType phosh_status_icon_get_type (void);

#define PLUGIN_NAME        "furios-folder-dock"
#define APP_GRID_TYPE      "PhoshAppGrid"
#define FOLDER_BUTTON_TYPE "PhoshAppGridFolderButton"
/* Ids from phosh's app-grid.ui. GtkBuilder gives every object with an id
   that name, template children included. */
#define APPS_ID            "apps"
#define SCROLLED_ID        "scrolled_window"

/* What a folder button has to offer for a copy of it to work: the folder it
   shows, and the signal the grid opens it through. */
#define FOLDER_INFO_PROP   "folder-info"
#define FOLDER_SIGNAL      "folder-launched"

/*
 * A crash of the shell ends the session, and with the plugin still listed
 * the next login would crash the same way. So a mark is written before the
 * grid is touched and taken away once the dock has stood for a while; a mark
 * that is still there at start means the last attempt did not survive, and
 * the plugin then does nothing until somebody switches it on again (the app
 * removes the mark when it does).
 */
#define GUARD_FILE         "furios-folder-dock.armed"
#define GUARD_CLEAR_S      15

/* The app grid is built with the overview, which may come after the top bar.
   Looked for this often and this many times, then given up on. */
#define FIND_INTERVAL_MS   2000
#define FIND_TRIES         30

/*
 * One dock for the whole shell. phosh may build a status icon more than
 * once (one per bar that shows them), so the dock belongs to the module and
 * the widgets only count: the first one in sets it up, the last one out
 * puts everything back.
 */
typedef struct {
  int         users;
  GtkWidget  *grid;       /* weak */
  GtkWidget  *apps;       /* weak: phosh's flowbox */
  GtkWidget  *dock;       /* ours: a revealer-less box, owned by its parent */
  GtkWidget  *flow;       /* ours: the flowbox inside it */
  GPtrArray  *pairs;      /* Pair: phosh's hidden button and our copy */
  guint       find_id;
  int         tries;
  guint       sync_id;
  gulong      alloc_id;
  guint       guard_id;
  gboolean    refused;
  gboolean    warned;

  /* Floating: the dock lies over the scrolled area in an overlay of ours,
     which takes the scrolled window's place in phosh's column. Everything
     needed to put that back exactly as it was. */
  GtkWidget  *overlay;    /* weak */
  GtkWidget  *scrolled;   /* weak */
  GtkWidget  *column;     /* weak */
  int         position;
  gboolean    expand, fill;
  guint       padding;
  GtkPackType pack;

  /* The scrolled content gets room at its end as tall as the dock, or its
     last row could never be scrolled out from under it. */
  GtkWidget  *content;    /* weak */
  int         content_margin;
  guint       pad_id;
  gulong      dock_alloc_id;
} Dock;

static Dock dock;


static gboolean
is_type (gpointer widget, const char *name)
{
  for (GType t = G_OBJECT_TYPE (widget); t; t = g_type_parent (t))
    if (strcmp (g_type_name (t), name) == 0)
      return TRUE;
  return FALSE;
}


static void
find_by_type_cb (GtkWidget *widget, gpointer user_data)
{
  GtkWidget **found = user_data;

  if (*found)
    return;
  if (is_type (widget, APP_GRID_TYPE)) {
    *found = widget;
    return;
  }
  if (GTK_IS_CONTAINER (widget))
    gtk_container_forall (GTK_CONTAINER (widget), find_by_type_cb, found);
}


typedef struct {
  const char *name;
  GtkWidget  *found;
} ByName;


static void
find_by_name_cb (GtkWidget *widget, gpointer user_data)
{
  ByName *by = user_data;
  const char *name;

  if (by->found)
    return;
  name = gtk_buildable_get_name (GTK_BUILDABLE (widget));
  if (name && strcmp (name, by->name) == 0) {
    by->found = widget;
    return;
  }
  if (GTK_IS_CONTAINER (widget))
    gtk_container_forall (GTK_CONTAINER (widget), find_by_name_cb, by);
}


static GtkWidget *
find_by_name (GtkWidget *root, const char *name)
{
  ByName by = { name, NULL };

  if (GTK_IS_CONTAINER (root))
    gtk_container_forall (GTK_CONTAINER (root), find_by_name_cb, &by);
  return by.found;
}


static GtkWidget *
find_app_grid (void)
{
  GList *tops = gtk_window_list_toplevels ();
  GtkWidget *found = NULL;

  for (GList *l = tops; l && !found; l = l->next)
    find_by_type_cb (l->data, &found);
  g_list_free (tops);
  return found;
}


static char *
guard_path (void)
{
  return g_build_filename (g_get_user_cache_dir (), GUARD_FILE, NULL);
}


static gboolean
on_guard_clear (gpointer user_data)
{
  g_autofree char *path = guard_path ();

  dock.guard_id = 0;
  g_unlink (path);
  return G_SOURCE_REMOVE;
}


/* --- the copies ---------------------------------------------------------- */

typedef struct {
  GtkWidget *original;    /* weak: phosh's button, hidden while we hold it */
  GtkWidget *copy;        /* weak: ours, in the dock's flowbox - which the
                             shell may tear down with the whole grid */
  gulong     destroy_id;
} Pair;

static void queue_sync (void);
static void queue_pad (void);


static void
on_original_destroyed (GtkWidget *original, gpointer user_data)
{
  /* A hidden child that phosh destroys does not make the flowbox allocate
     again, so this is the one change the allocation hook never hears of. */
  queue_sync ();
}


/* A copy was pressed: open the folder the way the original would. */
static void
on_copy_launched (GtkWidget *copy, GObject *info, gpointer user_data)
{
  Pair *pair = user_data;

  if (pair->original)
    g_signal_emit_by_name (pair->original, FOLDER_SIGNAL, info);
}


static void
pair_free (gpointer user_data)
{
  Pair *pair = user_data;

  if (pair->original) {
    g_signal_handler_disconnect (pair->original, pair->destroy_id);
    g_object_remove_weak_pointer (G_OBJECT (pair->original), (gpointer *) &pair->original);
  }
  if (pair->copy) {
    g_signal_handlers_disconnect_by_data (pair->copy, pair);
    g_object_remove_weak_pointer (G_OBJECT (pair->copy), (gpointer *) &pair->copy);
    gtk_widget_destroy (pair->copy);
  }
  g_free (pair);
}


static gboolean
can_copy (GtkWidget *original)
{
  GParamSpec *spec = g_object_class_find_property (G_OBJECT_GET_CLASS (original),
                                                   FOLDER_INFO_PROP);

  return spec != NULL &&
         (spec->flags & G_PARAM_READABLE) &&
         (spec->flags & (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT | G_PARAM_CONSTRUCT_ONLY)) &&
         G_TYPE_IS_OBJECT (spec->value_type) &&
         g_signal_lookup (FOLDER_SIGNAL, G_OBJECT_TYPE (original)) != 0;
}


/* The same type, the same folder: it looks and behaves like the original,
   down to renaming, because it reads the same folder info. */
static Pair *
make_pair (GtkWidget *original)
{
  Pair *pair;
  GObject *info = NULL;
  GtkWidget *copy;

  g_object_get (original, FOLDER_INFO_PROP, &info, NULL);
  if (info == NULL)
    return NULL;
  copy = g_object_new (G_OBJECT_TYPE (original), FOLDER_INFO_PROP, info, NULL);
  g_object_unref (info);

  pair = g_new0 (Pair, 1);
  pair->original = original;
  g_object_add_weak_pointer (G_OBJECT (original), (gpointer *) &pair->original);
  pair->destroy_id = g_signal_connect (original, "destroy",
                                       G_CALLBACK (on_original_destroyed), NULL);
  pair->copy = copy;
  g_object_add_weak_pointer (G_OBJECT (copy), (gpointer *) &pair->copy);
  g_signal_connect (copy, FOLDER_SIGNAL, G_CALLBACK (on_copy_launched), pair);
  gtk_flow_box_insert (GTK_FLOW_BOX (dock.flow), copy, -1);
  gtk_widget_show (copy);

  return pair;
}


static void
hide_original (GtkWidget *original)
{
  gtk_widget_set_no_show_all (original, TRUE);
  if (gtk_widget_get_visible (original))
    gtk_widget_hide (original);
}


static void
show_original (GtkWidget *original)
{
  gtk_widget_set_no_show_all (original, FALSE);
  gtk_widget_show (original);
}


/* phosh's folder buttons, in the grid's order. */
static GPtrArray *
folders_in_grid (void)
{
  GPtrArray *found = g_ptr_array_new ();
  GList *children = gtk_container_get_children (GTK_CONTAINER (dock.apps));

  for (GList *l = children; l; l = l->next) {
    if (!is_type (l->data, FOLDER_BUTTON_TYPE))
      continue;
    if (!can_copy (l->data)) {
      if (!dock.warned)
        g_warning (PLUGIN_NAME ": a folder button without '" FOLDER_INFO_PROP
                   "' or '" FOLDER_SIGNAL "' - leaving the folders alone");
      dock.warned = TRUE;
      continue;
    }
    g_ptr_array_add (found, l->data);
  }
  g_list_free (children);
  return found;
}


static void
sync_dock (void)
{
  GPtrArray *originals;
  gboolean same;

  if (!dock.apps || !dock.flow)
    return;

  originals = folders_in_grid ();
  same = originals->len == dock.pairs->len;
  for (guint i = 0; same && i < originals->len; i++) {
    Pair *pair = g_ptr_array_index (dock.pairs, i);

    same = pair->original == g_ptr_array_index (originals, i);
  }

  /* Anything different - a folder added, dropped, renamed into another
     place, or the whole list rebuilt - and the copies are made afresh.
     A handful of folders; not worth a finer diff. The originals that stay
     are hidden again right below, so none flashes up in between. */
  if (!same) {
    g_ptr_array_set_size (dock.pairs, 0);
    for (guint i = 0; i < originals->len; i++) {
      Pair *pair = make_pair (g_ptr_array_index (originals, i));

      if (pair)
        g_ptr_array_add (dock.pairs, pair);
    }
  }

  /* Only the ones with a copy: a folder we could not copy stays in view. */
  for (guint i = 0; i < dock.pairs->len; i++) {
    Pair *pair = g_ptr_array_index (dock.pairs, i);

    if (pair->original)
      hide_original (pair->original);
  }
  g_ptr_array_unref (originals);

  gtk_widget_set_visible (dock.dock, dock.pairs->len > 0);
  queue_pad ();
}


static gboolean
on_sync_idle (gpointer user_data)
{
  dock.sync_id = 0;
  sync_dock ();
  return G_SOURCE_REMOVE;
}


static void
queue_sync (void)
{
  if (dock.sync_id == 0 && dock.flow)
    dock.sync_id = g_idle_add (on_sync_idle, NULL);
}


static gboolean
on_pad_idle (gpointer user_data)
{
  int want;

  dock.pad_id = 0;
  if (!dock.content)
    return G_SOURCE_REMOVE;
  want = dock.content_margin;
  if (dock.dock && gtk_widget_get_visible (dock.dock))
    want += gtk_widget_get_allocated_height (dock.dock);
  /* Only on a change: a new margin allocates again, and that calls here. */
  if (gtk_widget_get_margin_bottom (dock.content) != want)
    gtk_widget_set_margin_bottom (dock.content, want);
  return G_SOURCE_REMOVE;
}


static void
queue_pad (void)
{
  if (dock.pad_id == 0)
    dock.pad_id = g_idle_add (on_pad_idle, NULL);
}


static void
on_dock_allocated (GtkWidget *widget, GdkRectangle *alloc, gpointer user_data)
{
  queue_pad ();
}


/* phosh's flowbox is re-allocated whenever its children change. Not the
   place to change widgets, so the work is done in the next idle - and when
   there is nothing to move, it changes nothing and the loop ends there. */
static void
on_apps_allocated (GtkWidget *apps, GdkRectangle *alloc, gpointer user_data)
{
  queue_sync ();
}


/* --- building and taking down the dock ----------------------------------- */

static gboolean
build_dock (GtkWidget *grid)
{
  GtkWidget *apps = find_by_name (grid, APPS_ID);
  GtkWidget *scrolled = find_by_name (grid, SCROLLED_ID);
  GtkWidget *column = scrolled ? gtk_widget_get_parent (scrolled) : NULL;
  GtkWidget *content;
  GtkCssProvider *css;

  if (!GTK_IS_FLOW_BOX (apps) || !GTK_IS_SCROLLED_WINDOW (scrolled) || !GTK_IS_BOX (column)) {
    g_warning (PLUGIN_NAME ": the app grid is not the shape this was written for - "
               "leaving it alone");
    return FALSE;
  }

  {
    g_autofree char *path = guard_path ();

    g_mkdir_with_parents (g_get_user_cache_dir (), 0700);
    g_file_set_contents (path, "", 0, NULL);
    dock.guard_id = g_timeout_add_seconds (GUARD_CLEAR_S, on_guard_clear, NULL);
  }

  dock.grid = grid;
  dock.apps = apps;
  g_object_add_weak_pointer (G_OBJECT (grid), (gpointer *) &dock.grid);
  g_object_add_weak_pointer (G_OBJECT (apps), (gpointer *) &dock.apps);

  dock.dock = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_valign (dock.dock, GTK_ALIGN_END);
  gtk_style_context_add_class (gtk_widget_get_style_context (dock.dock), PLUGIN_NAME);
  /* The apps pass underneath, so the bar needs a ground of its own to be
     read against - the theme's, not quite opaque. On this one widget only. */
  css = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (css,
                                   "." PLUGIN_NAME " {"
                                   " background-color: alpha(@theme_bg_color, 0.85); }",
                                   -1, NULL);
  gtk_style_context_add_provider (gtk_widget_get_style_context (dock.dock),
                                  GTK_STYLE_PROVIDER (css),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (css);

  /* The same spacing and margins as phosh's own grid, so the folders look
     the same down here as they did up there. */
  dock.flow = gtk_flow_box_new ();
  g_object_set (dock.flow,
                "homogeneous", TRUE,
                "selection-mode", GTK_SELECTION_NONE,
                "activate-on-single-click", FALSE,
                "column-spacing", 6,
                "row-spacing", 6,
                "margin-start", 3,
                "margin-end", 3,
                "margin-top", 6,
                "margin-bottom", 6,
                "halign", GTK_ALIGN_CENTER,
                "max-children-per-line", 8,
                NULL);
  gtk_container_add (GTK_CONTAINER (dock.dock), dock.flow);
  gtk_widget_show_all (dock.dock);
  gtk_widget_set_no_show_all (dock.dock, TRUE);

  /* The scrolled window moves into an overlay of ours, at its own place in
     the column and with its own packing, and the dock lies over its bottom
     edge. */
  gtk_container_child_get (GTK_CONTAINER (column), scrolled,
                           "position", &dock.position, NULL);
  gtk_box_query_child_packing (GTK_BOX (column), scrolled, &dock.expand,
                               &dock.fill, &dock.padding, &dock.pack);
  dock.overlay = gtk_overlay_new ();
  g_object_ref (scrolled);
  gtk_container_remove (GTK_CONTAINER (column), scrolled);
  gtk_container_add (GTK_CONTAINER (dock.overlay), scrolled);
  g_object_unref (scrolled);
  gtk_overlay_add_overlay (GTK_OVERLAY (dock.overlay), dock.dock);
  gtk_box_pack_start (GTK_BOX (column), dock.overlay, dock.expand, dock.fill, dock.padding);
  gtk_box_set_child_packing (GTK_BOX (column), dock.overlay, dock.expand,
                             dock.fill, dock.padding, dock.pack);
  gtk_box_reorder_child (GTK_BOX (column), dock.overlay, dock.position);
  gtk_widget_show (dock.overlay);

  dock.scrolled = scrolled;
  dock.column = column;
  g_object_add_weak_pointer (G_OBJECT (scrolled), (gpointer *) &dock.scrolled);
  g_object_add_weak_pointer (G_OBJECT (column), (gpointer *) &dock.column);
  g_object_add_weak_pointer (G_OBJECT (dock.overlay), (gpointer *) &dock.overlay);
  g_object_add_weak_pointer (G_OBJECT (dock.dock), (gpointer *) &dock.dock);
  g_object_add_weak_pointer (G_OBJECT (dock.flow), (gpointer *) &dock.flow);

  /* The box inside the scrolled window - through the viewport GTK puts
     around anything that cannot scroll by itself. */
  content = gtk_bin_get_child (GTK_BIN (scrolled));
  if (GTK_IS_VIEWPORT (content))
    content = gtk_bin_get_child (GTK_BIN (content));
  if (content) {
    dock.content = content;
    dock.content_margin = gtk_widget_get_margin_bottom (content);
    g_object_add_weak_pointer (G_OBJECT (content), (gpointer *) &dock.content);
  }
  dock.dock_alloc_id = g_signal_connect_after (dock.dock, "size-allocate",
                                               G_CALLBACK (on_dock_allocated), NULL);

  dock.alloc_id = g_signal_connect_after (apps, "size-allocate",
                                          G_CALLBACK (on_apps_allocated), NULL);
  sync_dock ();
  return TRUE;
}


static void
take_down_dock (void)
{
  /* Switched off or torn down in good order: nothing crashed. */
  if (dock.guard_id) {
    g_source_remove (dock.guard_id);
    on_guard_clear (NULL);
  }

  if (dock.find_id) {
    g_source_remove (dock.find_id);
    dock.find_id = 0;
  }
  if (dock.sync_id) {
    g_source_remove (dock.sync_id);
    dock.sync_id = 0;
  }
  if (dock.pad_id) {
    g_source_remove (dock.pad_id);
    dock.pad_id = 0;
  }
  if (dock.apps && dock.alloc_id)
    g_signal_handler_disconnect (dock.apps, dock.alloc_id);
  dock.alloc_id = 0;
  if (dock.dock && dock.dock_alloc_id)
    g_signal_handler_disconnect (dock.dock, dock.dock_alloc_id);
  dock.dock_alloc_id = 0;

  /* Every original back in view, every copy gone. */
  if (dock.pairs) {
    for (guint i = 0; i < dock.pairs->len; i++) {
      Pair *pair = g_ptr_array_index (dock.pairs, i);

      if (pair->original)
        show_original (pair->original);
    }
    g_ptr_array_set_size (dock.pairs, 0);
  }

  if (dock.dock)
    gtk_widget_destroy (dock.dock);

  /* The scrolled window back into phosh's column, where and how it was. */
  if (dock.scrolled && dock.overlay && dock.column &&
      gtk_widget_get_parent (dock.scrolled) == dock.overlay) {
    GtkWidget *scrolled = g_object_ref (dock.scrolled);

    gtk_container_remove (GTK_CONTAINER (dock.overlay), scrolled);
    gtk_widget_destroy (dock.overlay);
    gtk_box_pack_start (GTK_BOX (dock.column), scrolled, dock.expand, dock.fill, dock.padding);
    gtk_box_set_child_packing (GTK_BOX (dock.column), scrolled, dock.expand,
                               dock.fill, dock.padding, dock.pack);
    gtk_box_reorder_child (GTK_BOX (dock.column), scrolled, dock.position);
    g_object_unref (scrolled);
  } else if (dock.overlay) {
    gtk_widget_destroy (dock.overlay);
  }
  if (dock.content) {
    gtk_widget_set_margin_bottom (dock.content, dock.content_margin);
    g_object_remove_weak_pointer (G_OBJECT (dock.content), (gpointer *) &dock.content);
    dock.content = NULL;
  }
  if (dock.scrolled)
    g_object_remove_weak_pointer (G_OBJECT (dock.scrolled), (gpointer *) &dock.scrolled);
  if (dock.column)
    g_object_remove_weak_pointer (G_OBJECT (dock.column), (gpointer *) &dock.column);
  dock.scrolled = dock.column = NULL;

  if (dock.grid)
    g_object_remove_weak_pointer (G_OBJECT (dock.grid), (gpointer *) &dock.grid);
  if (dock.apps)
    g_object_remove_weak_pointer (G_OBJECT (dock.apps), (gpointer *) &dock.apps);
  dock.grid = dock.apps = NULL;
}


static gboolean
on_find_timeout (gpointer user_data)
{
  GtkWidget *grid = find_app_grid ();

  if (grid) {
    dock.find_id = 0;
    build_dock (grid);
    return G_SOURCE_REMOVE;
  }
  if (++dock.tries >= FIND_TRIES) {
    g_warning (PLUGIN_NAME ": no app grid found - leaving it alone");
    dock.find_id = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}


static gboolean
on_first_idle (gpointer user_data)
{
  if (dock.users > 0 && !dock.dock && !dock.find_id && !dock.refused) {
    dock.tries = 0;
    if (on_find_timeout (NULL) == G_SOURCE_CONTINUE)
      dock.find_id = g_timeout_add (FIND_INTERVAL_MS, on_find_timeout, NULL);
  }
  return G_SOURCE_REMOVE;
}


/* --- the status icon that carries it ------------------------------------- */

static void
on_destroy (GtkWidget *self)
{
  if (!g_object_get_data (G_OBJECT (self), PLUGIN_NAME))
    return;
  g_object_set_data (G_OBJECT (self), PLUGIN_NAME, NULL);
  if (--dock.users == 0)
    take_down_dock ();
}


static void
furios_folder_dock_init (GTypeInstance *instance, gpointer klass)
{
  GtkWidget *self = GTK_WIDGET (instance);

  if (!dock.pairs)
    dock.pairs = g_ptr_array_new_with_free_func (pair_free);

  /* Asked whenever no dock stands: then no mark can be this shell's own, so
     one that is there was left by a shell that did not survive. Asked again
     on every new instance, which is what switching it on again makes - and
     the app removes the mark when it does, so that is the way back. */
  if (!dock.dock) {
    g_autofree char *path = guard_path ();

    dock.refused = g_file_test (path, G_FILE_TEST_EXISTS);
    if (dock.refused)
      g_warning (PLUGIN_NAME ": %s is still there - the last attempt did not "
                 "survive, so the app grid is left alone", path);
  }

  /* Nothing to show in the bar, ever. */
  gtk_widget_set_no_show_all (self, TRUE);
  gtk_widget_hide (self);

  g_object_set_data (G_OBJECT (self), PLUGIN_NAME, GINT_TO_POINTER (1));
  dock.users++;
  g_signal_connect (self, "destroy", G_CALLBACK (on_destroy), NULL);

  /* Not from here: we are being built inside the shell's own setup of its
     top bar, and the overview may not exist yet. */
  g_idle_add (on_first_idle, NULL);
}


static GType
furios_folder_dock_get_type (void)
{
  static GType type = 0;

  if (g_once_init_enter (&type)) {
    GType parent = phosh_status_icon_get_type ();
    GTypeQuery query = { 0 };
    GTypeInfo info = { 0 };
    GType registered = 0;

    g_type_query (parent, &query);
    if (query.type == 0) {
      g_warning ("phosh's status icon type is not registered - no folder dock");
    } else {
      info.class_size = query.class_size;
      info.instance_size = query.instance_size;
      info.instance_init = furios_folder_dock_init;
      registered = g_type_register_static (parent, "FuriosFolderDock", &info, 0);
    }
    g_once_init_leave (&type, registered);
  }

  return type;
}


/* --- the GIO module, which is how phosh finds any of this ---------------- */

void
g_io_module_load (GIOModule *module)
{
  g_type_module_use (G_TYPE_MODULE (module));

  g_io_extension_point_implement (PHOSH_PLUGIN_EXTENSION_POINT_STATUS_ICON_WIDGET,
                                  furios_folder_dock_get_type (),
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
