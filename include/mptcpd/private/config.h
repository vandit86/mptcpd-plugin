/* include/mptcpd/private/config.h.  Generated from config.h.in by configure.  */
/* include/mptcpd/private/config.h.in.  Generated from configure.ac by autoheader.  */

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `asan' library (-lasan). */
/* #undef HAVE_LIBASAN */

/* Define to 1 if you have the `lsan' library (-llsan). */
/* #undef HAVE_LIBLSAN */

/* Define to 1 if you have the `ubsan' library (-lubsan). */
/* #undef HAVE_LIBUBSAN */

/* Define to 1 if you have the <linux/mptcp.h> header file. */
#define HAVE_LINUX_MPTCP_H 1

/* Define to 1 if you have the multipath-tcp.org kernel <linux/mptcp.h>
   header. */
/* #undef HAVE_LINUX_MPTCP_H_MPTCP_ORG */

/* Define to 1 if you have the upstream kernel <linux/mptcp.h> header. */
#define HAVE_LINUX_MPTCP_H_UPSTREAM 1

/* Define to 1 if <linux/mptcp.h> supports MPTCP genl events. */
/* #undef HAVE_LINUX_MPTCP_H_UPSTREAM_EVENTS */

/* ELL has l_genl_msg_get_extended_error() */
#define HAVE_L_GENL_MSG_GET_EXTENDED_ERROR /**/

/* ELL has l_hashmap_replace() */
#define HAVE_L_HASHMAP_REPLACE /**/

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Log message destination. */
#define MPTCPD_LOGGER stderr

/* Define if debugging is disabled */
/* #undef NDEBUG */

/* Name of package */
#define PACKAGE "mptcpd"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "mptcp@lists.linux.dev"

/* Define to the full name of this package. */
#define PACKAGE_NAME "mptcpd"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "mptcpd 0.8"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "mptcpd"

/* Define to the home page for this package. */
#define PACKAGE_URL "https://github.com/intel/mptcpd"

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.8"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Version number of package */
#define VERSION "0.8"
