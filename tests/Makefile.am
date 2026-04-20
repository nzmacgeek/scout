AM_CPPFLAGS = -I$(top_srcdir)/src -I$(top_builddir)

check_PROGRAMS = test-config test-dhcp
TESTS = $(check_PROGRAMS)

test_config_SOURCES = test_config.c
test_config_LDADD = $(top_builddir)/src/libscoutcommon.a

test_dhcp_SOURCES = test_dhcp.c
test_dhcp_LDADD = $(top_builddir)/src/libscoutcommon.a
