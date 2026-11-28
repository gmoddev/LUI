/* Windows x64 MSVC configuration for the pinned upstream libffi sources. */
#ifndef LUI_LIBFFI_MSVC_CONFIG_H
#define LUI_LIBFFI_MSVC_CONFIG_H
#define STDC_HEADERS 1
#define HAVE_MEMCPY 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define SIZEOF_DOUBLE 8
#define SIZEOF_LONG_DOUBLE 8
#define SIZEOF_SIZE_T 8
#ifdef LIBFFI_ASM
#define FFI_HIDDEN(Name)
#else
#define FFI_HIDDEN
#endif
#endif
