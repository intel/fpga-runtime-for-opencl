// Copyright (C) 2014-2021 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ACL_VISIBILITY_H
#define ACL_VISIBILITY_H

// These can be used to control visibility of the symbol at the library
// boundary.
// E.g. See use in hal/mmd/Makefile
//
//    hidden: Symbol not visible outside the library
//
//    default: Symbol always visible outside the library.
//          For ELF shared libarary, the symbol can also be "interposed",
//          i.e. replaced at runtime by the loader (e.g. via LD_PRELOAD)
//
//    protected: Visible outside the library, but clients inside
//          the same ELF .so can't see any interposed value.
//          That is, you'll always get the definition internal to the same
//          library.
// See http://gcc.gnu.org/onlinedocs/gcc-4.1.2/gcc/Function-Attributes.html

#if __GNUC__
#define ACL_VISIBILITY_DEFAULT __attribute__((visibility("default")))
#define ACL_VISIBILITY_HIDDEN __attribute__((visibility("hidden")))
#define ACL_VISIBILITY_PROTECTED __attribute__((visibility("protected")))
#else
#define ACL_VISIBILITY_DEFAULT
#define ACL_VISIBILITY_HIDDEN
#define ACL_VISIBILITY_PROTECTED
#endif

// Use ACL_EXPORT for symbols we definitely want to export, or which are
// used via function pointers.
#ifndef ACL_EXPORT
#define ACL_EXPORT ACL_VISIBILITY_DEFAULT
#endif

#endif // ACL_VISIBILITY_H
