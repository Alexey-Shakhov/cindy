#define ARRAY_DEF_FUNCTIONS(elem_type, array_type)               \
array_type array_type##_alloc(u32 capacity, Arena* arena) {      \
    return (array_type) {                                        \
        .capacity = capacity,                                    \
        .length = 0,                                             \
        .buf = arena_alloc(arena, sizeof(elem_type) * capacity)  \
    };                                                           \
}                                                                \
                                                                 \
void array_type##_append(array_type* array, elem_type elem) {    \
    if (array->length >= array->capacity) {                      \
        fatal("Array overflow\n");                               \
    }                                                            \
    array->buf[array->length++] = elem;                          \
}                                                                \

#define ARRAY_DEF(elem_type, array_type)   \
typedef struct {                           \
    u32 capacity;                          \
    u32 length;                            \
    elem_type* buf;                        \
} array_type;                              \
ARRAY_DEF_FUNCTIONS(elem_type, array_type) \

#define ARRAY_DEF_BY_ELEM(elem_type) ARRAY_DEF(elem_type, elem_type##_array)

ARRAY_DEF_BY_ELEM(float)
ARRAY_DEF_BY_ELEM(double)
ARRAY_DEF_BY_ELEM(u64)
ARRAY_DEF_BY_ELEM(u32)
ARRAY_DEF_BY_ELEM(u16)
ARRAY_DEF_BY_ELEM(u8)
ARRAY_DEF_BY_ELEM(i64)
ARRAY_DEF_BY_ELEM(i32)
ARRAY_DEF_BY_ELEM(i16)
ARRAY_DEF_BY_ELEM(i8)
