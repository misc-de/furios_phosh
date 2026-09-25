# phosh-folder-dock

Holds the folders of phosh's app overview in a bar at the bottom edge of the
screen. The bar lies over the apps, which scroll underneath it; the apps get
room at their end as tall as the bar, so the last row still comes clear.

phosh has no setting or plugin type for this: folders and apps share one
`GtkFlowBox` inside the scrolled area, and GTK 3's CSS can neither reorder nor
pin anything. So this is a status-icon plugin that shows nothing in the top
bar. While it is loaded it finds `PhoshAppGrid` in the shell's windows, puts
the scrolled area into an overlay of its own (same place, same packing) and
lays a bar over its bottom edge.

The folders in that bar are **copies**: phosh's own folder buttons stay in
the grid, only hidden. In phosh 0.55 a folder button *is* the flowbox child,
and the flowbox is bound to a list model, which drops children by index - a
button taken out would make phosh destroy the wrong launchers later. A copy
is the same type built from the same folder info, and pressing it emits
`folder-launched` on the hidden original, the signal phosh's grid opens
folders through. When the plugin is switched off, the copies go, the
originals come back into view, and the scrolled area goes back where it was.

This depends on the inside of phosh's app grid (checked against phosh 0.55):
the ids `apps` and `scrolled_window` from `app-grid.ui`, the type names
`PhoshAppGrid` and `PhoshAppGridFolderButton`, and the folder button's
`folder-info` property and `folder-launched` signal. If the grid does not look like
that, the plugin logs one warning and leaves it alone.

## Crash guard

A crash of phosh ends the session, and with the plugin still listed the next
login would crash the same way. Before touching the grid the plugin writes
`~/.cache/furios-folder-dock.armed` and removes it after 15 seconds of a
standing dock (or when switched off). If the file is there when the plugin
starts, it does nothing. Switching it on again in the app removes the file.

Way out by hand, over ssh: take `furios-folder-dock` out of

    gsettings get mobi.phosh.shell.plugins status-icons

and write the rest back with `gsettings set`, or run `./uninstall.sh`.

## Install, switch on, test

    ./install.sh                # WITHOUT sudo; picked up at the next reboot
    # then: misc-de app -> Other -> Folders at the bottom
    ./tests/run-tests.sh        # NEVER with sudo

The test builds the app grid's shape as phosh 0.55 has it (the folder button
is a `GtkFlowBoxChild`, the flowbox bound to a model) and checks that folders
show in the dock and still open, that every child of phosh's flowbox stays
at the index of its own item, that the dock follows the model when
phosh adds or drops a folder, that a second instance builds no second dock,
that everything goes back when the last one goes, and that a leftover crash
mark keeps the grid untouched.

## Uninstall

    ./uninstall.sh
