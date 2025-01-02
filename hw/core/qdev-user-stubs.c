#include "qemu/osdep.h"
#include "hw/qdev-core.h"

void qemu_create_machine(QDict *qdict)
{
    Object *fake_machine_obj;

    fake_machine_obj = object_property_add_new_container(object_get_root(),
                                                         "machine");
    object_property_add_new_container(fake_machine_obj, "unattached");
}
