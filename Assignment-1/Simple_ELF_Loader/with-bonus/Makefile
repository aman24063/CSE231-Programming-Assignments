#invoke make inside following directories and in this order: loader, launch, fib
#move the lib_simpleloader.so and launch binaries inside bin directory
#Provide the command for cleanup
all:
	mkdir -p bin
	$(MAKE) -C loader
	cp loader/lib_simpleloader.so bin/
	$(MAKE) -C launcher
	cp launcher/launch bin/
	$(MAKE) -C test

clean:
	-@rm -rf bin
	$(MAKE) -C loader clean
	$(MAKE) -C launcher clean
	$(MAKE) -C test clean
