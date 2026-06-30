#slangc shaders/bake.vert.slang -target spirv -stage vertex -emit-spirv-directly -o shaders/compiled/bake.vert.spirv
#slangc shaders/bake.frag.slang -target spirv -stage fragment -emit-spirv-directly -o shaders/compiled/bake.frag.spirv
slangc shaders/bake.slang -target spirv -o shaders/compiled/bake.spirv
