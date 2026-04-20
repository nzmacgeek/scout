AC_INIT([scout], [0.1.0], [maintainers@blueyos.example], [scout])
AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_SRCDIR([src/scoutd.c])
AC_CONFIG_HEADERS([config.h])
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([foreign subdir-objects dist-xz])
AM_PROG_AR
AC_PROG_CC
AC_PROG_RANLIB

AC_ARG_VAR([PACKAGE_BUILD_NUMBER], [dimsim package build number])
AS_IF([test -z "${PACKAGE_BUILD_NUMBER}"], [PACKAGE_BUILD_NUMBER=1])
AC_SUBST([PACKAGE_BUILD_NUMBER])

AC_ARG_WITH(
  [libc],
  [AS_HELP_STRING([--with-libc=@<:@auto|musl|glibc@:>@], [select the target libc])],
  [with_libc="${withval}"],
  [with_libc="auto"]
)
AS_CASE(
  ["${with_libc}"],
  [auto|musl|glibc], [],
  [AC_MSG_ERROR([unsupported libc '${with_libc}'])])
AC_SUBST([SCOUT_LIBC], ["${with_libc}"])

AC_ARG_WITH(
  [blueyos-sysroot],
  [AS_HELP_STRING([--with-blueyos-sysroot=PATH], [BlueyOS libc/sysroot prefix])],
  [SCOUT_BLUEYOS_SYSROOT="${withval}"],
  [SCOUT_BLUEYOS_SYSROOT=""]
)
AC_SUBST([SCOUT_BLUEYOS_SYSROOT])

AC_ARG_ENABLE(
  [blueyos-netctl],
  [AS_HELP_STRING([--enable-blueyos-netctl], [force BlueyOS netctl support])],
  [enable_blueyos_netctl="${enableval}"],
  [enable_blueyos_netctl="auto"]
)

AS_CASE(["${host_os}"], [*blueyos*], [scout_blueyos_host=yes], [scout_blueyos_host=no])

AC_CACHE_CHECK([whether the compiler defines __blueyos__], [scout_cv_blueyos_macro], [
  AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([], [[
#ifndef __blueyos__
#error __blueyos__ is not defined
#endif
    ]])],
    [scout_cv_blueyos_macro=yes],
    [scout_cv_blueyos_macro=no]
  )
])

AS_IF(
  [test "x${enable_blueyos_netctl}" = "xyes"],
  [scout_enable_blueyos_netctl=yes],
  [test "x${enable_blueyos_netctl}" = "xno"],
  [scout_enable_blueyos_netctl=no],
  [test "x${scout_blueyos_host}" = "xyes" || test "x${scout_cv_blueyos_macro}" = "xyes"],
  [scout_enable_blueyos_netctl=yes],
  [scout_enable_blueyos_netctl=no]
)

AS_IF([test -n "${SCOUT_BLUEYOS_SYSROOT}"], [
  CPPFLAGS="${CPPFLAGS} -isystem ${SCOUT_BLUEYOS_SYSROOT}/include"
  LDFLAGS="${LDFLAGS} -L${SCOUT_BLUEYOS_SYSROOT}/lib"
])

AC_CHECK_HEADERS([
  arpa/inet.h
  errno.h
  fcntl.h
  ifaddrs.h
  limits.h
  linux/if.h
  net/if.h
  net/route.h
  netdb.h
  netinet/ip.h
  netinet/ip_icmp.h
  signal.h
  stdbool.h
  stdint.h
  stdio.h
  stdlib.h
  string.h
  sys/ioctl.h
  sys/socket.h
  sys/stat.h
  sys/syscall.h
  sys/time.h
  sys/types.h
  syslog.h
  time.h
  unistd.h
])

AC_CHECK_FUNCS([
  clock_gettime
  getaddrinfo
  getifaddrs
  getnameinfo
  getopt_long
  inet_ntop
  inet_pton
  mkstemp
  poll
  select
  socket
  syslog
])

AC_DEFINE_UNQUOTED([SCOUT_LIBC_NAME], ["${with_libc}"], [Selected libc profile.])
AS_IF([test "x${scout_enable_blueyos_netctl}" = "xyes"], [
  AC_DEFINE([SCOUT_ENABLE_BLUEYOS_NETCTL], [1], [Build with BlueyOS netctl support.])
])

AM_CONDITIONAL([SCOUT_BLUEYOS_BUILD], [test "x${scout_enable_blueyos_netctl}" = "xyes"])

AC_CONFIG_FILES([
  Makefile
  src/Makefile
  tests/Makefile
])
AC_OUTPUT
