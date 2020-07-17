#ifndef XPETRA_CONFIG_HPP
#define XPETRA_CONFIG_HPP

/* #undef HAVE_XPETRA_DEBUG */

#define HAVE_XPETRA_TPETRA

#define HAVE_XPETRA_EPETRA

/* #undef HAVE_XPETRA_KOKKOSCLASSIC */

#define HAVE_XPETRA_EPETRAEXT

/* #undef HAVE_XPETRA_PROFILING */

#define HAVE_XPETRA_EXPERIMENTAL

/* #undef XPETRA_EPETRA_NO_32BIT_GLOBAL_INDICES */

#define XPETRA_EPETRA_NO_64BIT_GLOBAL_INDICES

/* Whether Xpetra enables LocalOrdinal = int and GlobalOrdinal = long long */
/* #undef HAVE_XPETRA_INT_LONG_LONG */

#endif // XPETRA_CONFIG_HPP
