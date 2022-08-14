#!/usr/bin/env bash

# Install dependencies
pacman -S --noconfirm mingw-w64-x86_64-pkg-config \
            mingw-w64-x86_64-libadwaita \
            mingw-w64-x86_64-gobject-introspection \
            mingw-w64-x86_64-python-gobject \
            mingw-w64-x86_64-enchant \
            mingw-w64-x86_64-editorconfig-core-c \
            mingw-w64-x86_64-meson \
            itstool \
            mingw-w64-x86_64-libssp \
            mingw-w64-x86_64-gcc \
            base-devel \
            mingw-w64-x86_64-toolchain
pacman -Syu --noconfirm

# Compile the application
LDFLAGS="-Wl,-lssp" meson setup builddir -Dforce_fallback_for=libadwaita,glib,gtk4,gtksourceview -Dlibadwaita:vapi=false -Dgtksourceview:vapi=false -Dgtk4:media-gstreamer=disabled
meson compile -C builddir
meson test -C builddir --suite gnome-text-editor
meson install -C builddir
