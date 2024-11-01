# System information #
What is your operating system and version? _(e.g. "Linux, Fedora 36" or "macOS 10.15.3")_

What is the version of GNOME Text Editor? _(e.g. "42.0-1.fc36.x86_64" or "commit 14e494")_

If the bug caused data loss, where was the file located? _(e.g. "Home directory" or "on a USB stick" or "in a directory symlinked from my Home directory")

```
Include the contents of About Text Editor->Troubleshooting->Debugging Information
here to provide us with detailed versions of foundational libraries and settings
used by your system.
```

<!--
It is very helpful to know if the bug also happens in the latest development version.
It can be installed alongside the regular version with these instructions:

1. Make sure that Flatpak is installed (see https://flatpak.org/setup )

2. Copy and run the following command in the Terminal or Console app:
   flatpak install --from https://nightly.gnome.org/repo/appstream/org.gnome.TextEditor.Devel.flatpakref

3. Launch the development version e.g. with:
   flatpak run org.gnome.TextEditor.Devel
-->
Have you tested Nightly to see if the issue has been fixed? If not, why?


# Bug information #
## Steps to reproduce ##
- Step by step, how can you make the problem appear?
- List those steps here.
- If the problem doesn't happen every time, note that as well.

Note that Feature Requests should first be filed at [Teams/Design/Whiteboards](https://gitlab.gnome.org/Teams/Design/whiteboards/) to start the design process.

## Current behaviour ##
What happened that made it evident there was a problem?
Copy and paste the exact text of any error messages.
Use code blocks (```) to format them.

Is there any error information in the system journal? You may be able
to find errors using (`sudo journalctl -xb`).

## Expected behaviour ##
What did you expect to see instead?

/label ~"1. Bug"
/label ~"2. Needs Diagnosis"
