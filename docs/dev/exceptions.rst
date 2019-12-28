Exceptions
==========
Layered file protocols uses exceptions to signal errors - errors automatically
propagate between layers, and are intercepted at the C-API boundary. When one
of the exceptions described here are raised, an error message for with
`lfp_errormsg()`, is set, and the exception is converted to the corresponding
error code.


.. code:: cpp

   // Example protocol where all records start with 'b'
   auto err = protocol->readinto(buf, len, &nread);
   if (err != LFP_OK)
      throw lfp::protocol_fatal("Truncated record");
   if (buf[0] != 'b') {
      auto msg = "Record does not start with 'b'";
      throw lfp::protocol_fatal(msg);
   }

:code:`#include <protocol.hpp>`

.. doxygenclass:: lfp::error
   :project: lfp

.. doxygengroup:: exceptions
   :project: lfp
   :outline:
