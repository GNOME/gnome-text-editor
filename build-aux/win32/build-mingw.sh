#!/usr/bin/env bash
set -e

# Update everything
pacman --noconfirm -Suy

# Install dependencies
pacman -S --noconfirm  base-devel zip \
            ${MINGW_PACKAGE_PREFIX}-pkgconf \
            ${MINGW_PACKAGE_PREFIX}-libadwaita \
            ${MINGW_PACKAGE_PREFIX}-gobject-introspection \
            ${MINGW_PACKAGE_PREFIX}-python-gobject \
            ${MINGW_PACKAGE_PREFIX}-enchant \
            ${MINGW_PACKAGE_PREFIX}-editorconfig-core-c \
            ${MINGW_PACKAGE_PREFIX}-meson \
            ${MINGW_PACKAGE_PREFIX}-itstool \
            ${MINGW_PACKAGE_PREFIX}-gcc \
            ${MINGW_PACKAGE_PREFIX}-gtk4 \
            ${MINGW_PACKAGE_PREFIX}-gtksourceview5 \
            ${MINGW_PACKAGE_PREFIX}-libspelling \
            ${MINGW_PACKAGE_PREFIX}-gtk-update-icon-cache \
            ${MINGW_PACKAGE_PREFIX}-shaderc \
            ${MINGW_PACKAGE_PREFIX}-vulkan \
            ${MINGW_PACKAGE_PREFIX}-vulkan-headers

# Compile the application
MSYS2_ARG_CONV_EXCL="--prefix=" meson setup builddir --prefix="${MINGW_PREFIX}" --optimization=g \
        -Dgtk:media-gstreamer=disabled -Dgtk:x11-backend=false -Dgtk:wayland-backend=false -Dgtk:win32-backend=true -Dgtk:werror=false \
        -Dlibadwaita:vapi=false -Dlibadwaita:examples=false -Dlibadwaita:tests=false -Dlibadwaita:werror=false \
        -Dgtksourceview:vapi=false \
        -Dlibspelling:docs=false -Dlibspelling:sysprof=false -Dlibspelling:vapi=false
meson compile -C builddir
meson test -C builddir --suite gnome-text-editor

# Copy required binaries into builddir
cp -R ${MINGW_PREFIX}/bin/gdbus.exe builddir
cp -R ${MINGW_PREFIX}/bin/lib*.dll builddir
cp -R ${MINGW_PREFIX}/bin/zlib1.dll builddir

cd builddir
mkdir -p portable_install${MINGW_PREFIX}/{bin,etc,lib,share}
mv *.dll *.exe ./portable_install${MINGW_PREFIX}/bin/
DESTDIR=./portable_install meson install
cd portable_install${MINGW_PREFIX}

# Check for existence of gnometexteditor exe to check if compilation succeeded. If not exit with an error.
CHECK_FILE="./bin/gnome-text-editor.exe"
if test -f "$CHECK_FILE"
then
    echo "$CHECK_FILE exists. Windows build can proceed."
else
    echo "$CHECK_FILE does not exist. Compilation failed."
    exit 1
fi

# Copy required resources into portable_install
cp -RTn ${MINGW_PREFIX}/etc/fonts ./etc/fonts
cp -RTn ${MINGW_PREFIX}/lib/gdk-pixbuf-2.0 ./lib/gdk-pixbuf-2.0
cp -RTn ${MINGW_PREFIX}/share/glib-2.0 ./share/glib-2.0
cp -RTn ${MINGW_PREFIX}/share/icons/Adwaita ./share/icons/Adwaita
cp -RTn ${MINGW_PREFIX}/share/icons/hicolor ./share/icons/hicolor
cp -RTn ${MINGW_PREFIX}/share/gtksourceview-5 ./share/gtksourceview-5

# Compile the schemas to prevent issues because of their absence
glib-compile-schemas.exe ./share/glib-2.0/schemas
gtk4-update-icon-cache.exe ./share/icons/hicolor

# Write batch file to execute gte with proper environment variables
echo "@echo off" >> ./execute_gte_proper_environent_variables.bat
echo "set GSETTINGS_SCHEMA_DIR=%~dp0\share\glib-2.0\schemas" >> ./execute_gte_proper_environent_variables.bat
echo "bin\gnome-text-editor.exe --standalone" >> ./execute_gte_proper_environent_variables.bat

zip -r ../../../gnome-text-editor_${version}_$(uname -m).zip ./*
