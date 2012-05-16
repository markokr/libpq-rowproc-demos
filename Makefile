
pg = /opt/apps/pgsql92
AM_CPPFLAGS = -I$(pg)/include
AM_LDFLAGS = -L$(pg)/lib -Wl,-rpath=$(pg)/lib
AM_LIBS = -lpq

AM_CPPFLAGS += '-DCONNSTR="dbname=postgres"'

noinst_PROGRAMS = demo-rowproc-sync demo-rowproc-async \
		  demo-onerow-sync demo-onerow-async \
		  test-sync test-async test-plus

test_sync_SOURCES = test-sync.c
test_async_SOURCES = test-async.c
test_plus_SOURCES = test-plus.cc

demo_rowproc_sync_SOURCES = demo-rowproc-sync.c
demo_rowproc_async_SOURCES = demo-rowproc-async.c

demo_onerow_sync_SOURCES = demo-onerow-sync.c
demo_onerow_async_SOURCES = demo-onerow-async.c

include antimake.mk

#antimake.mk:
#	wget -q https://raw.github.com/markokr/libusual/master/mk/antimake.mk

