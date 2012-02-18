
pg = /opt/apps/pgsql92
AM_CPPFLAGS = -I$(pg)/include
AM_LDFLAGS = -L$(pg)/lib -Wl,-rpath=$(pg)/lib
AM_LIBS = -lpq

AM_CPPFLAGS += '-DCONNSTR="dbname=postgres"'

noinst_PROGRAMS = sync async plus

sync_SOURCES = sync.c
async_SOURCES = async.c
plus_SOURCES = plus.cc


include antimake.mk

antimake.mk:
	wget -q https://raw.github.com/markokr/libusual/master/mk/antimake.mk

