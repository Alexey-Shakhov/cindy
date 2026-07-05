cc = gcc
cflags = -g -pthread -Wall -Wno-unused-variable
includes = -Ilibs -Ilibs/cglm/include
links = -lstdc++ -lglfw -lvulkan -lm -lktx

build/vma_impl.o:
	g++ -c vma_impl.cpp -std=c++17 -O2 -o build/vma_impl.o

build/shaders/bake.spirv:
	mkdir -p build/shaders
	slangc shaders/bake.slang -target spirv -o build/shaders/bake.spirv

offline: build/vma_impl.o build/shaders/bake.spirv
	mkdir -p build/offline-output
	$(cc) $(cflags) offline.c build/vma_impl.o $(includes) $(links) -o build/offline

realtime: build/vma_impl.o build/shaders/bake.spirv
	$(cc) $(cflags) realtime.c build/vma_impl.o $(includes) $(links) -o build/realtime

.PHONY: clean
clean:
	rm -rf build/*
