# Text Editor TODO

## EditorSession

Currently the session removes drafts that are not left open when exiting.
This is problematic because we can lose information.
We need to track this for longer as a sort of Recent Manager.

## EditorSidebar

The sidebar needs to fix the bullet placement.

The sidebar also needs to show recent documents once that is added to the
EditorSession object.

It would be nice to animate the sidebar in and out. Also, when dragging
the sidebar to the edge, if the :position is < min-width size request,
then we should hide the sidebar.

## EditorPage

We need support for Search & Replace which should be ported from Builder

## EditorPageSettings

We need support for .editorconfig, modeline, and metadata from syntax files.
We also need to add reasonable defaults to GtkSourceLanguage metadata.
Automatic right margin position will depend on these values.

## EditorDocument

We want to auto-save to drafts in the background so that if we crash, we
can still recover the incremental work.

We need to monitor the file for changes and show an infobar.

## EditorWindow

The file open dialog needs to setup some default filters (plain text).

## help-overlay.ui

We need to add lots of the shortcuts that are port of GtkSourceView and
GtkTextView to the shortcuts window.

## Pastebin

Upload to pastebin still needs to be implemented.

## Adwaita Style Scheme

This is forked from Builder's, and it still needs a lot of tweaking to
adapt to the new Adwaita/GNOME palette.
