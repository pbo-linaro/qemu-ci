#ifndef GDBSTUB_H
#define GDBSTUB_H

typedef struct GDBFeature {
    const char *xmlname;
    const char *xml;
    const char *name;
    const char * const *regs;
    int num_regs;
} GDBFeature;

typedef struct GDBFeatureBuilder {
    GDBFeature *feature;
    GPtrArray *xml;
    GPtrArray *regs;
    int base_reg;
} GDBFeatureBuilder;


/* Get or set a register.  Returns the size of the register.  */
typedef int (*gdb_get_reg_cb)(CPUState *cpu, GByteArray *buf, int reg);
typedef int (*gdb_set_reg_cb)(CPUState *cpu, uint8_t *buf, int reg);

/**
 * gdb_init_cpu(): Initialize the CPU for gdbstub.
 * @cpu: The CPU to be initialized.
 */
void gdb_init_cpu(CPUState *cpu);

/**
 * gdb_register_coprocessor() - register a supplemental set of registers
 * @cpu - the CPU associated with registers
 * @get_reg - get function (gdb reading)
 * @set_reg - set function (gdb modifying)
 * @num_regs - number of registers in set
 * @xml - xml name of set
 * @gpos - non-zero to append to "general" register set at @gpos
 */
void gdb_register_coprocessor(CPUState *cpu,
                              gdb_get_reg_cb get_reg, gdb_set_reg_cb set_reg,
                              const GDBFeature *feature, int g_pos);

/**
 * gdb_unregister_coprocessor_all() - unregisters supplemental set of registers
 * @cpu - the CPU associated with registers
 */
void gdb_unregister_coprocessor_all(CPUState *cpu);

/**
 * gdbserver_start: start the gdb server
 * @port_or_device: connection spec for gdb
 * @errp: error handle
 *
 * For CONFIG_USER this is either a tcp port or a path to a fifo. For
 * system emulation you can use a full chardev spec for your gdbserver
 * port.
 *
 * The error handle should be either &error_fatal (for start-up) or
 * &error_warn (for QMP/HMP initiated sessions).
 *
 * Returns true when server successfully started.
 */
bool gdbserver_start(const char *port_or_device, Error **errp);

/**
 * gdb_feature_builder_init() - Initialize GDBFeatureBuilder.
 * @builder: The builder to be initialized.
 * @feature: The feature to be filled.
 * @name: The name of the feature.
 * @xmlname: The name of the XML.
 * @base_reg: The base number of the register ID.
 */
void gdb_feature_builder_init(GDBFeatureBuilder *builder, GDBFeature *feature,
                              const char *name, const char *xmlname,
                              int base_reg);

/**
 * gdb_feature_builder_append_tag() - Append a tag.
 * @builder: The builder.
 * @format: The format of the tag.
 * @...: The values to be formatted.
 */
void G_GNUC_PRINTF(2, 3)
gdb_feature_builder_append_tag(const GDBFeatureBuilder *builder,
                               const char *format, ...);

/**
 * gdb_feature_builder_append_reg() - Append a register.
 * @builder: The builder.
 * @name: The register's name; it must be unique within a CPU.
 * @bitsize: The register's size, in bits.
 * @regnum: The offset of the register's number in the feature.
 * @type: The type of the register.
 * @group: The register group to which this register belongs; it can be NULL.
 */
void gdb_feature_builder_append_reg(const GDBFeatureBuilder *builder,
                                    const char *name,
                                    int bitsize,
                                    int regnum,
                                    const char *type,
                                    const char *group);

/**
 * gdb_feature_builder_end() - End building GDBFeature.
 * @builder: The builder.
 */
void gdb_feature_builder_end(const GDBFeatureBuilder *builder);

/**
 * gdb_find_static_feature() - Find a static feature.
 * @xmlname: The name of the XML.
 *
 * Return: The static feature.
 */
const GDBFeature *gdb_find_static_feature(const char *xmlname);

/**
 * gdb_read_register() - Read a register associated with a CPU.
 * @cpu: The CPU associated with the register.
 * @buf: The buffer that the read register will be appended to.
 * @reg: The register's number returned by gdb_find_feature_register().
 *
 * Return: The number of read bytes.
 */
int gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);

/**
 * gdb_write_register() - Write a register associated with a CPU.
 * @cpu: The CPU associated with the register.
 * @buf: The buffer that the register contents will be set to.
 * @reg: The register's number returned by gdb_find_feature_register().
 *
 * The size of @buf must be at least the size of the register being
 * written.
 *
 * Return: The number of written bytes, or 0 if an error occurred (for
 * example, an unknown register was provided).
 */
int gdb_write_register(CPUState *cpu, uint8_t *mem_buf, int reg);

/**
 * typedef GDBRegDesc - a register description from gdbstub
 */
typedef struct {
    int gdb_reg;
    const char *name;
    const char *feature_name;
} GDBRegDesc;

/**
 * gdb_get_register_list() - Return list of all registers for CPU
 * @cpu: The CPU being searched
 *
 * Returns a GArray of GDBRegDesc, caller frees array but not the
 * const strings.
 */
GArray *gdb_get_register_list(CPUState *cpu);

void gdb_set_stop_cpu(CPUState *cpu);

/* in gdbstub-xml.c, generated by scripts/feature_to_c.py */
extern const GDBFeature gdb_static_features[];

#endif /* GDBSTUB_H */
