#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include "cglm/cglm.h"
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

typedef struct Primitive {
    // u32 texture_id;
    u32 index_offset;
    u32 index_count;
} Primitive;

typedef struct Mesh {
    Primitive* primitives;
    u32 primitives_count;
} Mesh;

typedef struct Node {
    u32 index;
    int parent_index;
    Mesh* mesh;

    bool has_matrix;
    mat4 matrix;
    vec3 translation;
    versor rotation;
    vec3 scale;
} Node;

typedef struct Vertex {
    vec3 pos;
    vec3 normal;
} Vertex;

typedef u32 vert_index;

typedef struct Scene {
    Mesh* meshes;
    size_t mesh_count;
    Node* nodes;
    size_t node_count;

    Vertex* vertices;
    size_t vertex_count;
    vert_index* indices;
    size_t index_count;
} Scene;

void node_make_matrix(Node* node)
{
    glm_mat4_identity(node->matrix);
    glm_translate(node->matrix, node->translation);
    glm_quat_rotate(node->matrix, node->rotation, node->matrix);
    glm_scale(node->matrix, node->scale);
    node->has_matrix = true;
}
 
Scene load_gltf_scene(const char* filename, Arena* arena) {
    Scene scene = {0};
    // Open the file
    cgltf_options gltf_options = {0};
    cgltf_data* gltf_data;
    cgltf_result gltf_result = cgltf_parse_file(&gltf_options, filename, &gltf_data);
    if (gltf_result != cgltf_result_success) {
        fatal("Failed to load GLTF.");
    }
    gltf_result = cgltf_load_buffers(&gltf_options, gltf_data, filename);
    if (gltf_result != cgltf_result_success) {
        fatal("Failed to load GLTF buffers.");
    }

    scene.meshes = arena_alloc(arena, sizeof(Mesh) * gltf_data->meshes_count);
    scene.mesh_count = gltf_data->meshes_count;
    scene.nodes = arena_alloc(arena, sizeof(Node) * gltf_data->nodes_count);
    scene.node_count = gltf_data->nodes_count;

    // Precalculate index and vertex buffer sizes
    size_t index_count = 0;
    size_t vertex_count = 0;
    for (size_t i=0; i < gltf_data->meshes_count; i++) {
        cgltf_mesh* gltf_mesh = &gltf_data->meshes[i];
        for (size_t p=0; p < gltf_mesh->primitives_count; p++) {
            cgltf_primitive* gltf_primitive = &gltf_mesh->primitives[p];
            assert(gltf_primitive->type == cgltf_primitive_type_triangles);

            for (size_t a=0; a < gltf_primitive->attributes_count; a++) {
                cgltf_attribute* attribute = &gltf_primitive->attributes[a];
                cgltf_accessor* accessor = attribute->data;
                if (attribute->type == cgltf_attribute_type_position) {
                    vertex_count += accessor->count;
                }
            }
            index_count += gltf_primitive->indices->count;
        }
    }

    scene.vertices = arena_alloc(arena, vertex_count * sizeof(Vertex));
    scene.vertex_count = vertex_count;
    scene.indices = arena_alloc(arena, index_count * sizeof(vert_index));
    scene.index_count = index_count;

    size_t index_offset = 0;
    size_t vertex_offset = 0;
    for (size_t i=0; i < gltf_data->meshes_count; i++) {
        cgltf_mesh* gltf_mesh = &gltf_data->meshes[i];
        Mesh* mesh = &scene.meshes[i];
        mesh->primitives_count = gltf_mesh->primitives_count;
        mesh->primitives = arena_alloc(arena, sizeof(Primitive) * mesh->primitives_count);
        // Primitives
        for (size_t p=0; p < gltf_mesh->primitives_count; p++) {
            cgltf_primitive* gltf_primitive = &gltf_mesh->primitives[p];

            // Material
            /*
            size_t material_index = cgltf_material_index(gltf_data, gltf_primitive->material);
            mesh->primitives[p].texture_id = material_index;
            */

            // Vertices
            size_t primitive_vertex_count;
            for (size_t a=0; a < gltf_primitive->attributes_count; a++) {
                cgltf_attribute* attribute = &gltf_primitive->attributes[a];
                cgltf_accessor* accessor = attribute->data;
                size_t count = accessor->count;
                size_t stride = accessor->stride;
                cgltf_buffer_view* buffer_view = accessor->buffer_view;
                cgltf_buffer* buffer = buffer_view->buffer;
                char* data = (char*) buffer->data + buffer_view->offset + accessor->offset;
                
                if (attribute->type == cgltf_attribute_type_position) {
                    for (size_t v=0; v < count; v++) {
                        vec3* pos = (vec3*) data;
                        scene.vertices[vertex_offset + v].pos[0] = (*pos)[0];
                        scene.vertices[vertex_offset + v].pos[1] = (*pos)[1];
                        scene.vertices[vertex_offset + v].pos[2] = (*pos)[2];
                        data += stride;
                    }
                    primitive_vertex_count = count;
                }
                
                if (attribute->type == cgltf_attribute_type_normal) {
                    for (size_t v=0; v < count; v++) {
                        vec3* normal = (vec3*) data;
                        scene.vertices[vertex_offset + v].normal[0] = (*normal)[0];
                        scene.vertices[vertex_offset + v].normal[1] = (*normal)[1];
                        scene.vertices[vertex_offset + v].normal[2] = (*normal)[2];
                        data += stride;
                    }
                }
                
                /*
                if (attribute->type == cgltf_attribute_type_texcoord) {
                    for (size_t v=0; v < count; v++) {
                        vec2* texcoord = (vec2*) data;
                        scene.vertices[vertex_offset + v].tex_coord[0] = (*texcoord)[0];
                        scene.vertices[vertex_offset + v].tex_coord[1] = (*texcoord)[1];
                        data += stride;
                    }
                }
                */
            }

            // Indices
            mesh->primitives[p].index_offset = index_offset;
            cgltf_accessor* index_accessor = gltf_primitive->indices;
            mesh->primitives[p].index_count = index_accessor->count;
            
            for (size_t i=0; i < index_accessor->count; i++) {
                scene.indices[index_offset + i] = cgltf_accessor_read_index(index_accessor, i) + vertex_offset;
            }

            vertex_offset += primitive_vertex_count;
            index_offset += index_accessor->count;
        }
    }
    
    // Load nodes
    cgltf_node* gltf_nodes = gltf_data->nodes;
    scene.node_count = gltf_data->nodes_count;
    scene.nodes = arena_alloc(arena, sizeof(Node) * scene.node_count);
    memset(scene.nodes, 0, sizeof(Node) * scene.node_count);

    for (size_t n=0; n < scene.node_count; n++) {
        Node* node = &scene.nodes[n];
        cgltf_node* gltf_node = &gltf_nodes[n];
        node->index = cgltf_node_index(gltf_data, gltf_node);

        if (gltf_node->has_matrix) {
            node->has_matrix = true;
            memcpy(node->matrix, gltf_node->matrix, sizeof(float) * 16);
        } else {
            node->has_matrix = false;
            if (gltf_node->has_translation) {
                glm_vec3_copy(gltf_node->translation, node->translation);
            } else {
                glm_vec3_zero(node->translation);
            }
            if (gltf_node->has_rotation) {
                glm_quat_copy(gltf_node->rotation, node->rotation);
            } else {
                glm_quat_identity(node->rotation);
            }
            if (gltf_node->has_scale) {
                glm_vec3_copy(gltf_node->scale, node->scale);
            } else {
                glm_vec3_one(node->scale);
            }
        }

        if (gltf_node->mesh) {
            size_t mesh_index = cgltf_mesh_index(gltf_data, gltf_node->mesh);
            node->mesh = &scene.meshes[mesh_index];
        }

        if (gltf_node->parent) {
            node->parent_index = cgltf_node_index(gltf_data, gltf_node->parent);
        } else {
            node->parent_index = -1;
        }
    }

    cgltf_free(gltf_data);

    for (int i=0; i < scene.node_count; i++) {
        if (!scene.nodes[i].has_matrix) {
            node_make_matrix(&scene.nodes[i]);
        }
    }
    
    return scene;
}
