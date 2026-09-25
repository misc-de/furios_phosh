# phosh-folder-dock

Holds the folders of phosh's app overview in a bar at the bottom edge of the
screen, so they stay put while the apps scroll above them.

phosh has no setting or plugin type for this: folders and apps share one
`GtkFlowBox` inside the scrolled area, and GTK 3's CSS can neither reorder nor
pin anything. So this is a status-icon plugin that shows nothing in the top
bar. While it is loaded it finds `PhoshAppGrid` in the shell's windows, adds a
bar under the scrolled area and **moves** phosh's own folder buttons into it.
A moved button still opens its folder, because the connection that does that
travels with the widget. When the plugin is switched off, every button goes
back where it came from.

This depends on the inside of phosh's app grid (checked against phosh 0.55):
the ids `apps` and `scrolled_window` from `app-grid.ui` and the type names
`PhoshAppGrid` and `PhoshAppGridFolderButton`. If the grid does not look like
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

The test builds the app grid's shape with stand-in types and checks that
folders go to the dock and still open, that the dock follows the model when
phosh adds or drops a folder, that a second instance builds no second dock,
that everything goes back when the last one goes, and that a leftover crash
mark keeps the grid untouched.

## Uninstall

    ./uninstall.sh
