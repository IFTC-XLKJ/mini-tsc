#ifndef NODE_CTYPE_H
#define NODE_CTYPE_H

#include "runtime.h"

typedef struct CType {
    int32_t type_tag;  /* 0x43545950 = 'CTYP' */
    TSString* name;
    int32_t size;
    int32_t alignment;
    int is_signed;
} CType;

#define CTYPE_TAG 0x43595450

Value node_ctype_new_ctype(TSString* name, double size, double alignment, int isSigned);
Value node_ctype_sizeof(Value type);
Value node_ctype_alignof(Value type);
Value node_ctype_get_name(Value type);
Value node_ctype_get_size(Value type);
Value node_ctype_get_alignment(Value type);
Value node_ctype_get_signed(Value type);

#endif /* NODE_CTYPE_H */
