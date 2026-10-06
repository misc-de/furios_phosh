# folder-dock

Holds the folders of phosh's app overview in a bar at the bottom edge of the
screen, under the same line phosh draws below its favorites. The bar lies
over the apps, which scroll underneath it blurred; the apps get room at their
end as tall as the bar, so the last row still comes clear.

GTK 3 has no backdrop blur, so the plugin draws its overlay itself: the apps
everywhere except under the bar, then a copy of what lies under the bar,
drawn at a quarter of the size, box-blurred and scaled back up, then the
bar. The rectangle has to be left out of the sharp drawing - the copy is
partly clear where icons lay over the wallpaper, and the sharp ones would
show through. The wallpaper itself stays sharp: phosh draws it on a surface
of its own that nothing in here reaches.

## Settings

By default the folders take as many rows as they need, and every app shows
its name. With

By default the folders take as many rows as they need. With

    # ~/.config/furios-folder-dock.conf
    [dock]
    one-row=true
    hide-labels=true

the folders stand in a single row that scrolls sideways (without a
scrollbar), and the apps in the overview show only their icons, the way
phosh shows its favorites: the plugin hides the button's `label` child, the
same widget phosh hides for a favorite. Folders keep their names, and so do
the apps inside a folder - that is a flowbox of its own, which the plugin
never looks into.

The plugin watches the file while the dock stands, so a change takes effect
at once; the misc-de app writes it (Phosh -> "Folders in one row", "Hide app
names"). Switching the dock off gives every name back.

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
the ids `apps` and `scrolled_window` from `app-grid.ui`, `label` from
`app-grid-base-button.ui`, the type names
`PhoshAppGrid` and `PhoshAppGridFolderButton`, and the folder button's
`folder-info` property and `folder-launched` signal. If the grid does not look like
that, the plugin logs one warning and leaves it alone.

## Crash guard

A crash of phosh ends the session, and with the plugin still listed the next
login would crash the same way. Before touching the grid the plugin writes
`~/.cache/furios-folder-dock.armed` and removes it after 15 seconds of a
standing dock (or when switched off, or when the shell ends in good order -
`systemctl restart phosh`, a logout: phosh quits through exit(), and a
destructor takes the mark away, which a crash never runs). If the file is
there when the plugin starts, it does nothing. Switching it on again in the app removes the file.

Way out by hand, over ssh: take `furios-folder-dock` out of

    gsettings get mobi.phosh.shell.plugins status-icons

and write the rest back with `gsettings set`, or run `./uninstall.sh`.

## Install, switch on, test

    ./install.sh                # WITHOUT sudo; picked up at the next reboot
    # then: misc-de app -> Phosh -> Folders at the bottom
    ./tests/run-tests.sh        # NEVER with sudo

The test builds the app grid's shape as phosh 0.55 has it (the folder button
is a `GtkFlowBoxChild`, the flowbox bound to a model) and checks that folders
show in the dock and still open, that every child of phosh's flowbox stays
at the index of its own item, that the dock follows the model when
phosh adds or drops a folder, that a second instance builds no second dock,
that everything goes back when the last one goes, and that a leftover crash
mark keeps the grid untouched. It renders the overlay over a checkerboard with
clear squares and checks that the apps stay sharp above the bar and that
nothing sharp shows through under it, and that hidden names stay hidden for
apps added later while folders keep theirs.

## Uninstall

    ./uninstall.sh

Puts phosh's plugin list back to what it was before the first install -
recorded then, not guessed now; see "What was there before" in the
[top README](../README.md).
