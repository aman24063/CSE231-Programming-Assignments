#Create lib_simpleloader.so from loader.c

#Provide the command for cleanup
all:
	gcc -m32 -fPIC -c loader.c -o loader.o
	gcc -m32 -shared -o lib_simpleloader.so loader.o

clean:
	-@rm -f loader.o lib_simpleloader.so
