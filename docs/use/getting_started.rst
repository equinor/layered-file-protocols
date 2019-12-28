Getting started
===============
This short guide explains how to get started.

First example - cat.c
---------------------
A simple cat program, with minimal error checking. It simply outputs everything
in a file to stdout.

.. literalinclude:: ../../examples/cat.c

Building with cmake
-------------------
Layered file protocols provides a cmake config, so add this to your
CMakeLists.txt:

.. code::

   find_package(lfp REQUIRED)

   add_library(app app.c)
   target_link_libraries(app lfp::lfp)

Second example - tif-cat.c
--------------------------
Let us extend the cat program, by adding support for tape image format (TIF)
files.

.. literalinclude:: ../../examples/tif-cat.c

This program is almost identical to cat.c, but the the tapeimage protocol is
layered over the cfile. Notice how the main loop is unmodified, yet the program
now outputs the contents of a file sans the tape marks. In fact, this is a
pretty useful tool on its own for *stripping* tape marks from files.
