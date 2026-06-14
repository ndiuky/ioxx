#pragma once

#if defined(IOXX_SHARED)
#   if defined(_WIN32) || defined(__CYGWIN__)
#       if defined(IOXX_EXPORTS)
#           define IOXX_API __declspec(dllexport)
#       else
#           define IOXX_API __declspec(dllimport)
#       endif
#   else
#       if defined(IOXX_EXPORTS)
#           define IOXX_API __attribute__((visibility("default")))
#       else
#           define IOXX_API
#       endif
#   endif
#else
#   define IOXX_API
#endif
