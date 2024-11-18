/*
 * Device Container
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu/module.h"

static const TypeInfo container_info = {
    .name          = TYPE_CONTAINER,
    .parent        = TYPE_OBJECT,
};

static void container_register_types(void)
{
    type_register_static(&container_info);
}

/**
 * container_get(): Get the container object under specific path
 *
 * @root: The root path object to start walking from.  When starting from
 *        root, one can pass in object_get_root().
 * @path: The sub-path to lookup, must be an non-empty string starts with "/".
 *
 * Returns: The container object specified by @path.
 *
 * NOTE: the function may impplicitly create internal containers when the
 * whole path is not yet created.  It's the caller's responsibility to make
 * sure the path specified is always used as object containers, rather than
 * any other type of objects.
 */
Object *container_get(Object *root, const char *path)
{
    Object *obj, *child;
    char **parts;
    int i;

    parts = g_strsplit(path, "/", 0);
    /* "path" must be an non-empty string starting with "/" */
    assert(parts != NULL && parts[0] != NULL && !parts[0][0]);
    obj = root;

    for (i = 1; parts[i] != NULL; i++, obj = child) {
        child = object_resolve_path_component(obj, parts[i]);
        if (!child) {
            child = object_new(TYPE_CONTAINER);
            object_property_add_child(obj, parts[i], child);
            object_unref(child);
        } else {
            /*
             * Each object within the path must be a container object
             * itself, including the object to be returned.
             */
            assert(object_dynamic_cast(child, TYPE_CONTAINER));
        }
    }

    g_strfreev(parts);

    return obj;
}


type_init(container_register_types)
