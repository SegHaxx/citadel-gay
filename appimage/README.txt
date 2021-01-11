****************************************************************************************
**  DO NOT RUN THE APPIMAGE BUILD ON A PRODUCTION MACHINE!  IT WILL ERASE YOUR DATA!  **
****************************************************************************************

The tooling in this directory can be used to build an AppImage, the entire Citadel System
distributed as a single binary file.  Visit https://appimage.org/ to learn more about the
AppImage format and how it works.

Again, do NOT try to run this on your production machine.  For that matter, don't try to
run it on anything other than a dedicated build host.  It will ERASE /usr/local/citadel
and /usr/local/webcit during the build process.

If you're an end user you shouldn't have any need to do this at all.  The whole point of
this is that we can supply ready-to-run binaries that will run on any Linux/Linux system
without modification or dependencies.  If you are an end user, stop here, go download the
binary package, and use it.  Enjoy it and have fun.

Still with us?  Then you must be a new member of the Citadel team and you're packaging
the software for a new architecture or something.  So here's what you have to do to build
the binary:

1. Download the Citadel source tree (if you're reading this, you've already done that).
2. Install all system dependencies.  The same ones needed for Easy Install are fine.
3. Download and install "appimagetool" from appimage.org.
4. Run "./build_appimage.sh"

What's going to happen next?

1. Any existing /usr/local/citadel and /usr/local/webcit will be erased.
2. The script will go through the source tree, building and installing libcitadel,
   the Citadel server, and WebCit into the /usr/local hierarchy.
3. All binaries, static data, and libraries will be copied into the citadel.AppDir
   tree.
4. appimagetool will be called, and it will generate an executable with a
   name like "Citadel-x64.AppImage".   This is your distributable binary.  Upload
   it somewhere fun.

You should be running this build on the OLDEST version of Linux/Linux on which your
binary should be able to run.  The distribution does not matter -- for example, a
binary built on Debian should run fine on Ubuntu or Red Hat or whatever -- but the C
library and other very base system libraries are only upward compatible, not downward
compatible.  For example, at the time of this writing, I am building on Ubuntu 14 and
it's early 2021.
