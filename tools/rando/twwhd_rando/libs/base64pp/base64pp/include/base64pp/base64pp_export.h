
#ifndef BASE64PP_EXPORT_H
#define BASE64PP_EXPORT_H

#ifdef BASE64PP_STATIC_DEFINE
#  define BASE64PP_EXPORT
#  define BASE64PP_NO_EXPORT
#else
#  ifndef BASE64PP_EXPORT
#    ifdef base64pp_EXPORTS
        /* We are building this library */
#      define BASE64PP_EXPORT 
#    else
        /* We are using this library */
#      define BASE64PP_EXPORT 
#    endif
#  endif

#  ifndef BASE64PP_NO_EXPORT
#    define BASE64PP_NO_EXPORT 
#  endif
#endif

#ifndef BASE64PP_DEPRECATED
#  define BASE64PP_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef BASE64PP_DEPRECATED_EXPORT
#  define BASE64PP_DEPRECATED_EXPORT BASE64PP_EXPORT BASE64PP_DEPRECATED
#endif

#ifndef BASE64PP_DEPRECATED_NO_EXPORT
#  define BASE64PP_DEPRECATED_NO_EXPORT BASE64PP_NO_EXPORT BASE64PP_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef BASE64PP_NO_DEPRECATED
#    define BASE64PP_NO_DEPRECATED
#  endif
#endif

#endif /* BASE64PP_EXPORT_H */
