cc = gcc
cflags = -g -pthread -Wall -Wno-unused-variable
includes = -Ilibs -Ilibs/cglm/include -Ilibs/cgltf
links = -lstdc++ -lglfw -lvulkan -lm -lshaderc_combined -lglslang -lSPIRV-Tools -lSPIRV-Tools-opt -lpthread

build/vma_impl.o: vma_impl.cpp libs/vk_mem_alloc.h
	g++ -c vma_impl.cpp -std=c++17 -O2 -o build/vma_impl.o

build/shaders/bake.spirv: shaders/bake.slang
	mkdir -p build/shaders
	slangc shaders/bake.slang -target spirv -o build/shaders/bake.spirv

build/shaders/realtime.spirv: shaders/realtime.slang
	mkdir -p build/shaders
	slangc shaders/realtime.slang -target spirv -o build/shaders/realtime.spirv

offline: offline.c build/vma_impl.o build/shaders/bake.spirv
	mkdir -p build/offline-output
	$(cc) $(cflags) offline.c build/vma_impl.o $(includes) $(links) -o build/offline

realtime: realtime.c build/vma_impl.o build/shaders/realtime.spirv
	$(cc) $(cflags) realtime.c build/vma_impl.o $(includes) $(links) -o build/realtime

.PHONY: clean
clean:
	rm -rf build/*
