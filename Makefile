CFLAGS=-Wall -O0 -g
# CFLAGS+=-I../dist/public/nss/ -I../dist/Debug/include/nspr/
CFLAGS+=-I../dist/Linux5.1_x86_64_cc_glibc_PTH_64_DBG.OBJ/include/ -I../dist/public/nss/ -I../dist/Debug/include/nspr/
CFLAGS+=-DGSD_SMARTCARD_MANAGER_NSS_DB=\"/etc/pki/nssdb/\"
LDIR=-L../dist/Debug/lib/
# LDIR=-Wl,-rpath,/home/jmassey/Documents/projects/nss/dist/Debug/lib
LIBS=-lnss3 -lnspr4

all: tester

tester: tester.c
	gcc $(CFLAGS) $(LDIR) $(LIBS) $^ -o $@

clean:
	rm tester
