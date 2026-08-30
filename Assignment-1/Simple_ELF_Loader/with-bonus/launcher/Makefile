#Compile the launch.c by linking it with the lib_simpleloader.so

#Provide the command for cleanup
all:
	gcc -m32 -I../loader -o launch launch.c -L../bin -l_simpleloader -Wl,-rpath,'$$ORIGIN'

clean:
	-@rm -f launch
