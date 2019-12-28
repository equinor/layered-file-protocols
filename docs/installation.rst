Installation
============
Layered file protocols is under heavy development, and is only available in
source form. When the project matures, pre-compiled binaries will be provided.

Dependencies
------------
Run time:

- fmtlib https://github.com/fmtlib/fmt

Build time:

- C++11 compiler (tested on gcc, clang, and msvc)
- cmake https://cmake.org/

Documentation:

- Doxygen http://www.doxygen.nl/
- Sphinx http://www.sphinx-doc.org/en/master/
- Breathe https://breathe.readthedocs.io/en/latest/

From source with cmake
----------------------
Layered file protocols is a cookie-cutter cmake project:

.. code::

   mkdir build
   cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
   cmake --build build --target install

To build with fmtlib in header-only mode, for when you do not want a shared
libfmt dependency, pass :code:`-DLFP_FMT_HEADER_ONLY=TRUE` to cmake when
configuring.

To build the documentation, you need doxygen, sphinx, and breathe. To have it
built automatically, pass :code:`-DBUILD_DOC=TRUE` to cmake. Sphinx is invoked
through python, and cmake looks for python2 first. If you only have sphinx for
python3 (and you should), help cmake find the correct python by invoking cmake
with :code:`-DPYTHON_EXECUTABLE=`which python3``
