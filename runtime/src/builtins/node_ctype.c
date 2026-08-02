#include "node_ctype.h"
#include "ts_features.h"

/* Helper: determine C type size from a Value based on its runtime tag */
static int get_value_type_size(Value val) {
    switch (val.tag) {
        case TAG_STRING: return 1;   /* char */
        case TAG_NUMBER: return 4;   /* int (32-bit) */
        case TAG_BOOLEAN: return 1;  /* bool */
        case TAG_NULL: return 0;
        case TAG_OBJECT: return 8;   /* pointer */
        case TAG_ARRAY: return 8;    /* pointer */
        case TAG_FUNCTION: return 8; /* pointer */
        case TAG_SYMBOL: return 4;
        default: return 0;
    }
}

/* Helper: determine C type alignment from a Value */
static int get_value_type_alignment(Value val) {
    switch (val.tag) {
        case TAG_STRING: return 1;   /* char */
        case TAG_NUMBER: return 4;   /* int (32-bit) */
        case TAG_BOOLEAN: return 1;  /* bool */
        case TAG_NULL: return 1;
        case TAG_OBJECT: return 8;   /* pointer */
        case TAG_ARRAY: return 8;    /* pointer */
        case TAG_FUNCTION: return 8; /* pointer */
        case TAG_SYMBOL: return 4;
        default: return 1;
    }
}

/* Helper: determine if a Value is signed based on its runtime tag */
static int get_value_type_signed(Value val) {
    switch (val.tag) {
        case TAG_STRING: return 0;    /* char is unsigned by default */
        case TAG_NUMBER: return 1;    /* int is signed */
        case TAG_BOOLEAN: return 0;   /* bool is unsigned */
        case TAG_NULL: return 0;
        case TAG_OBJECT: return 0;    /* pointer is unsigned */
        case TAG_ARRAY: return 0;
        case TAG_FUNCTION: return 0;
        case TAG_SYMBOL: return 0;
        default: return 0;
    }
}

/* Helper: get type name from a Value */
static TSString* get_value_type_name(Value val) {
    switch (val.tag) {
        case TAG_STRING: return ts_string_new("char");
        case TAG_NUMBER: return ts_string_new("int");
        case TAG_BOOLEAN: return ts_string_new("bool");
        case TAG_NULL: return ts_string_new("null");
        case TAG_OBJECT: return ts_string_new("pointer");
        case TAG_ARRAY: return ts_string_new("array");
        case TAG_FUNCTION: return ts_string_new("function");
        case TAG_SYMBOL: return ts_string_new("symbol");
        default: return ts_string_new("unknown");
    }
}

/* CType constructor */
Value node_ctype_new_ctype(TSString* name, double size, double alignment, int isSigned) {
    CType* ctype = (CType*)ts_gc_alloc_kind(sizeof(CType), GC_KIND_RAW);
    ctype->type_tag = CTYPE_TAG;
    ctype->name = name;
    ctype->size = (int32_t)size;
    ctype->alignment = (int32_t)alignment;
    ctype->is_signed = isSigned;
    return ts_value_object(ctype);
}

/* sizeof: returns the size of the type based on runtime Value */
Value node_ctype_sizeof(Value type) {
    /* If it's a CType object, return its size */
    if (type.tag == TAG_OBJECT && type.as.object) {
        CType* ctype = (CType*)type.as.object;
        if (ctype->type_tag == CTYPE_TAG) {
            return ts_value_number((double)ctype->size);
        }
    }
    /* Otherwise, determine size from the Value type */
    return ts_value_number((double)get_value_type_size(type));
}

/* alignof: returns the alignment of the type */
Value node_ctype_alignof(Value type) {
    /* If it's a CType object, return its alignment */
    if (type.tag == TAG_OBJECT && type.as.object) {
        CType* ctype = (CType*)type.as.object;
        if (ctype->type_tag == CTYPE_TAG) {
            return ts_value_number((double)ctype->alignment);
        }
    }
    /* Otherwise, determine alignment from the Value type */
    return ts_value_number((double)get_value_type_alignment(type));
}

/* Get name property */
Value node_ctype_get_name(Value type) {
    if (type.tag == TAG_OBJECT && type.as.object) {
        CType* ctype = (CType*)type.as.object;
        if (ctype->type_tag == CTYPE_TAG) {
            return ts_value_string(ctype->name);
        }
    }
    return ts_value_string(get_value_type_name(type));
}

/* Get size property */
Value node_ctype_get_size(Value type) {
    if (type.tag == TAG_OBJECT && type.as.object) {
        CType* ctype = (CType*)type.as.object;
        if (ctype->type_tag == CTYPE_TAG) {
            return ts_value_number((double)ctype->size);
        }
    }
    return ts_value_number((double)get_value_type_size(type));
}

/* Get alignment property */
Value node_ctype_get_alignment(Value type) {
    if (type.tag == TAG_OBJECT && type.as.object) {
        CType* ctype = (CType*)type.as.object;
        if (ctype->type_tag == CTYPE_TAG) {
            return ts_value_number((double)ctype->alignment);
        }
    }
    return ts_value_number((double)get_value_type_alignment(type));
}

/* Get signed property */
Value node_ctype_get_signed(Value type) {
    if (type.tag == TAG_OBJECT && type.as.object) {
        CType* ctype = (CType*)type.as.object;
        if (ctype->type_tag == CTYPE_TAG) {
            return ts_value_boolean(ctype->is_signed);
        }
    }
    return ts_value_boolean(get_value_type_signed(type));
}
