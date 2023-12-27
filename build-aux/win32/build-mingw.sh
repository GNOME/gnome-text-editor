
#!/usr/bin/env bash
pacman -Sy
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
            mingw-w64-ucrt-x86_64-libwebp \
            mingw-w64-x86_64-curl \
            mingw-w64-x86_64-autotools \
            mingw-w64-x86_64-binutils \
            bison \
            mingw-w64-x86_64-libyaml \
            mingw-w64-x86_64-libstemmer \
            mingw-w64-x86_64-appstream \
            coreutils

            #it wasn't installing pkgconfig because base-devel was installing pkgconf. We'll install everything but that manually



#update all packages
pacman -Syu --noconfirm --ignore=pacman
# Compile the application
meson setup builddir  -Dlibadwaita:vapi=false -Dgtksourceview:vapi=false -Dgtk4:media-gstreamer=disabled
meson compile -C builddir
meson test -C builddir --suite gnome-text-editor
#move required files into builddir
cp -R /mingw64/bin/gdbus.exe builddir
cp -R /mingw64/bin/glib-compile-schemas.exe builddir
cp -R /mingw64/bin/lib*.dll builddir
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
#check for existence of gnometexteditor exe to check if compilation succeeded. If not exit with an error.
CHECK_FILE="./bin/gnome-text-editor.exe"
if test -f "$CHECK_FILE"
then
    echo "$CHECK_FILE exists. Windows build can proceed."
else
    echo "$CHECK_FILE does not exist. Compilation failed."
    exit 1
fi



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
#echo "set GSETTINGS_SCHEMA_DIR=%~dp0\share\glib-2.0\schemas" >> ./execute_gte_proper_environent_variables.bat
#echo "glib-compile-schemas.exe share\glib-2.0\schemas" >> ./execute_gte_proper_environent_variables.bat
echo "bin\gnome-text-editor.exe --standalone --exit-after-startup" >> ./execute_gte_proper_environent_variables.bat
pwd
mv ./execute_gte_proper_environent_variables ../
timeout 1m ./execute_gte_proper_environent_variables.bat
zip -r ../../../../gnome-text-editor_${version}_$(uname -m).zip ./*
