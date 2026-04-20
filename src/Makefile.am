sbindir = /sbin

AM_CPPFLAGS = -I$(top_srcdir)/src -I$(top_builddir)

noinst_LIBRARIES = libscoutcommon.a

libscoutcommon_a_SOURCES = \
	common.c \
	common.h \
	scout_blueyos_netctl.c \
	scout_blueyos_netctl.h \
	scout_config.c \
	scout_config.h \
	scout_dhcp.c \
	scout_dhcp.h \
	scout_files.c \
	scout_files.h \
	scout_platform.c \
	scout_platform.h \
	scout_types.h

sbin_PROGRAMS = scoutd
bin_PROGRAMS = nslookup ping tracert

scoutd_SOURCES = scoutd.c
scoutd_LDADD = libscoutcommon.a

nslookup_SOURCES = nslookup.c
nslookup_LDADD = libscoutcommon.a

ping_SOURCES = ping.c
ping_LDADD = libscoutcommon.a

tracert_SOURCES = tracert.c
tracert_LDADD = libscoutcommon.a
