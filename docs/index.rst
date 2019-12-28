Introduction
------------
   Any problem in Computer Science can be solved with another level of
   indirection

Layered file protocols is a high-level interface for byte-oriented file handles
with *layering*. File descriptors are composed and stacked at run-time, adding
features such as pre-fetching, indices, cloud access, and file format support
at each level.

Layered file protocols, lfp, provides

- A simple interface for application and library developers
- Multiple protocols for different file implementations and formats
- A developer interface for adding new protocols with C++

https://github.com/equinor/layered-file-protocols/

License
-------
Layered file protocols is licensed under the LGPL. See the LICENSE file for
details.

.. toctree::
   :caption: INSTALLATION
   :maxdepth: 1

   installation
   changelog

.. toctree::
   :caption: USAGE
   :maxdepth: 3

   use/getting_started

.. toctree::
   :caption: API REFERENCE
   :maxdepth: 3

   api/design
   api/functions
   api/status

.. toctree::
   :caption: PROTOCOLS
   :maxdepth: 3

   protocols/cfile
   protocols/tapeimage

.. toctree::
   :caption: PROTOCOL DEVELOPMENT
   :maxdepth: 3

   dev/protocol
   dev/interface
   dev/exceptions
   dev/automation

Why not use FILE?
-----------------
Layered file protocols was design to scratch our own itch - we want to deal
with files that *sometimes* use the tapeimage protocol, but not always. With
lfp, our applications can have a simple and straight-forward code path, and not
really notice the presence of tape markers and other such nonsense. There is no
standard way to add new drivers to FILE, except really being on the inside of
libc.

Technical details
-----------------
The interface is deliberately different from the C file library. While it does
make porting from FILE slightly more work, there are subtle differences in
guarantees and usage, which means it is necessary for lfp to detach itself from
the FILE interface.

The consumer interface is a C API. This is to provide the best and most
ergonomic experience for creating bindings for other languages, and to have an
easier time maintaing binary compatibility. Public functions will never be
removed, but may be deprecated.

Protocols are layered with `dependency injection`_, and are implemented in C++.
For an API reference, a description, and rationale, see the :ref:`Writing
protocols` section.

Many protocols are provided and bundled with lfp, and while it is sometimes
useful for very application-specific protocols to be implemented and maintained
in-application, the most sustainable approach is to contribute them upstream
and maintain them in the lfp tree.

.. _FUSE: https://en.wikipedia.org/wiki/Filesystem_in_Userspace
.. _dependency injection: https://en.wikipedia.org/wiki/Dependency_injection
