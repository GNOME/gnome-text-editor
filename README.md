# GNOME Text Editor

Text Editor is a simple text editor that focuses on session management.
It works hard to keep track of changes and state even if you quit the application.
You can come back to your work even if you've never saved it to a file.

## Contributions

 * __Please do not file issues for feature requests.__ Features must go through the design process first. File an issue at [Teams/Design/Whiteboards](https://gitlab.gnome.org/Teams/Design/whiteboards/) to start that process. In the end you should have a mockup, implementation specification, and breakdown of how that will change the existing code-base.
 * Please __test Nightly before filing bugs__. You can install Nightly from Flatpak using `flatpak --user install gnome-nightly org.gnome.TextEditor.Devel`.

## Features

 * A simple editor focused on a solid default experience
 * Syntax highlighting for common programming languages
 * Search & Replace including support for PCRE2-based regular expressions
 * Inline spell checking
 * Document printing
 * Desktop integration including dark-mode and saving session state
 * Support for .editorconfig and modelines
 * Integrated support for Vim keybindings

## Dependencies

 * GTK 4.15 or newer
 * GtkSourceView 5.10 or newer
 * Libadwaita 1.6.0 or newer
 * Libspelling 0.4.0 or newer
 * libeditorconfig for .editorconfig support

Refer to the [org.gnome.TextEditor.Devel.json](https://gitlab.gnome.org/GNOME/gnome-text-editor/tree/master/org.gnome.TextEditor.Devel.json) Flatpak manifest for additional details.

## Plans

To be a delightful and pleasing default text editor experience for GNOME.

## Try it Out!

You can install our Nightly build from Flatpak using [org.gnome.TextEditor.Devel.flatpakref](https://nightly.gnome.org/repo/appstream/org.gnome.TextEditor.Devel.flatpakref).
