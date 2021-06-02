CXX = g++ -fPIC
NETLIBS= -lnsl
CC=gcc

all: git-commit myhttpd daytime-server use-dlopen hello.so

jj-mod.o: jj-mod.c
	$(CC) -c jj-mod.c -fPIC

util.o: util.c
	$(CC) -c util.c -fPIC

jj-mod.so: jj-mod.o util.o
	ld -G -o jj-mod.so jj-mod.o util.o -fPIC

daytime-server : daytime-server.o
	$(CXX) -o $@ $@.o $(NETLIBS)

myhttpd : myhttpd.o
	$(CXX) -o $@ $@.o $(NETLIBS) -lpthread -ldl

use-dlopen: use-dlopen.o
	$(CXX) -o $@ $@.o $(NETLIBS) -ldl

hello.so: hello.o
	ld -G -o hello.so hello.o

%.o: %.cc
	@echo 'Building $@ from $<'
	$(CXX) -o $@ -c -I. $<

.PHONY: git-commit
git-commit:
	git checkout
	git add *.cc *.h Makefile >> .local.git.out  || echo
	git commit -a -m 'Commit' >> .local.git.out || echo
	git push origin master 

.PHONY: clean
clean:
	rm -f *.o use-dlopen hello.so
	rm -f *.o daytime-server myhttpd
