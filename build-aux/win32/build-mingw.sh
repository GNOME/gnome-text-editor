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
            mingw-w64-x86_64-gcc \
            base-devel \
            mingw-w64-x86_64-toolchain \
            mingw-w64-x86_64-icu \
            mingw-w64-x86_64-gettext \
            mingw-w64-x86_64-pango \
            mingw-w64-x86_64-cairo \
            mingw-w64-x86_64-libiconv \
            mingw-w64-x86_64-gcc-libs \
            mingw-w64-x86_64-pcre2 \
            mingw-w64-x86_64-fontconfig \
            mingw-w64-x86_64-pixman \
            mingw-w64-x86_64-freetype \
            mingw-w64-x86_64-libwinpthread-git \
            mingw-w64-x86_64-gtk4 \
            mingw-w64-x86_64-libpng \
            mingw-w64-x86_64-fribidi \
            mingw-w64-x86_64-harfbuzz \
            mingw-w64-x86_64-libthai \
            mingw-w64-x86_64-libffi \
            mingw-w64-x86_64-gtksourceview5 \
            mingw-w64-x86_64-glib2 \
            mingw-w64-x86_64-gsettings-desktop-schemas \
            mingw-w64-x86_64-zlib \
            zip \
            mingw-w64-ucrt-x86_64-libwebp 

#update all packages
pacman -Syu --noconfirm --ignore=pacman
# Compile the application
meson setup builddir  -Dlibadwaita:vapi=false -Dgtksourceview:vapi=false -Dgtk4:media-gstreamer=disabled
meson compile -C builddir
meson test -C builddir --suite gnome-text-editor
#move required files into builddir
cp -R /mingw64/bin/gdbus.exe builddir
cp -R /mingw64/bin/glib-compile-schemas.exe builddir
cp -R /mingw64/bin/libadwaita-1-0.dll builddir
cp -R /mingw64/bin/libbrotlicommon.dll builddir
cp -R /mingw64/bin/libbrotlidec.dll builddir
cp -R /mingw64/bin/libbz2-1.dll builddir
cp -R /mingw64/bin/libcairo-2.dll builddir
cp -R /mingw64/bin/libcairo-gobject-2.dll builddir
cp -R /mingw64/bin/libcairo-script-interpreter-2.dll builddir
cp -R /mingw64/bin/libdatrie-1.dll builddir
cp -R /mingw64/bin/libdeflate.dll builddir
cp -R /mingw64/bin/libeditorconfig.dll builddir
cp -R /mingw64/bin/libenchant-2.dll builddir 
cp -R /mingw64/bin/libepoxy-0.dll builddir
cp -R /mingw64/bin/libexpat-1.dll builddir
cp -R /mingw64/bin/libffi-8.dll builddir
cp -R /mingw64/bin/libfontconfig-1.dll builddir
cp -R /mingw64/bin/libfreetype-6.dll builddir
cp -R /mingw64/bin/libfribidi-0.dll builddir
cp -R /mingw64/bin/libgcc_s_seh-1.dll builddir
cp -R /mingw64/bin/libgdk_pixbuf-2.0-0.dll builddir
cp -R /mingw64/bin/libgio-2.0-0.dll builddir
cp -R /mingw64/bin/libglib-2.0-0.dll builddir
cp -R /mingw64/bin/libgmodule-2.0-0.dll builddir
cp -R /mingw64/bin/libgobject-2.0-0.dll builddir
cp -R /mingw64/bin/libgraphene-1.0-0.dll builddir
cp -R /mingw64/bin/libgraphite2.dll builddir
cp -R /mingw64/bin/libgtk-4-1.dll builddir
cp -R /mingw64/bin/libgtksourceview-5-0.dll builddir
cp -R /mingw64/bin/libharfbuzz-0.dll builddir
cp -R /mingw64/bin/libiconv-2.dll builddir
cp -R /mingw64/bin/libicudt72.dll builddir
cp -R /mingw64/bin/libicuuc72.dll builddir
cp -R /mingw64/bin/libintl-8.dll builddir
cp -R /mingw64/bin/libjbig-0.dll builddir
cp -R /mingw64/bin/libjpeg-8.dll builddir
cp -R /mingw64/bin/libLerc.dll builddir
cp -R /mingw64/bin/liblzma-5.dll builddir
cp -R /mingw64/bin/liblzo2-2.dll builddir
cp -R /mingw64/bin/libpango-1.0-0.dll builddir
cp -R /mingw64/bin/libpangocairo-1.0-0.dll builddir
cp -R /mingw64/bin/libpangoft2-1.0-0.dll builddir
cp -R /mingw64/bin/libpangowin32-1.0-0.dll builddir
cp -R /mingw64/bin/libpcre2-8-0.dll builddir
cp -R /mingw64/bin/libpixman-1-0.dll builddir
cp -R /mingw64/bin/libpng16-16.dll builddir
cp -R /mingw64/bin/libsharpyuv-0.dll builddir
cp -R /mingw64/bin/libstdc++-6.dll builddir
cp -R /mingw64/bin/libthai-0.dll builddir
cp -R /mingw64/bin/libtiff-6.dll builddir
cp -R /mingw64/bin/libwebp-7.dll builddir
cp -R /mingw64/bin/libwinpthread-1.dll  builddir
cp -R /mingw64/bin/libxml2-2.dll builddir
cp -R /mingw64/bin/libxml2-2.dll builddir
cp -R /mingw64/bin/libzstd.dll builddir
cp -R /mingw64/bin/zlib1.dll builddir
cp -R /mingw64/etc/fonts/fonts.conf builddir
cp -R /mingw64/share/fontconfig builddir
cp -R /mingw64/share/gtksourceview-5 builddir

  
#cp -R /mingw64/share/glib-2.0
cd builddir
mkdir -p portable_install/msys64/mingw64
mv *.dll *.exe ./gtksourceview-5 ./fontconfig ./portable_install/msys64/mingw64
mv fonts.conf ./portable_install/msys64/mingw64
DESTDIR=./portable_install meson install
cd portable_install/msys64/mingw64
mv *.dll *.exe bin
mv ./gtksourceview-5 ./fontconfig share
mkdir -p etc/fonts
mv fonts.conf etc/fonts
cp -rn /mingw64/share/glib-2.0/schemas/* ./share/glib-2.0/schemas

#compile the schemas to prevent issues because of their absence
bin/glib-compile-schemas.exe share/glib-2.0/schemas
bin/glib-compile-schemas.exe /mingw64/share/glib-2.0/schemas/
#write batch file to execute gte with proper environment variables
echo "@echo off" >> ./execute_gte_proper_environent_variables.bat
echo "set GSK_RENDERER=cairo"  >> ./execute_gte_proper_environent_variables.bat
echo "bin\gnome-text-editor.exe --standalone --exit-after-startup" >> ./execute_gte_proper_environent_variables.bat
timeout 1m ./execute_gte_proper_environent_variables.bat
zip -r ../../../../gnome-text-editor_${version}_$(uname -m).zip ./*
