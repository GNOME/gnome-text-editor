# Text Editor TODO

## EditorSidebar

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

## help-overlay.ui

We need to add lots of the shortcuts that are port of GtkSourceView and
GtkTextView to the shortcuts window.

## Pastebin

Upload to pastebin still needs to be implemented.
