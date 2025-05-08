/*
 * S390x DIAG instruction helper functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "hw/watchdog/wdt_diag288.h"
#include "system/cpus.h"
#include "hw/s390x/cert-store.h"
#include "hw/s390x/ipl.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "system/kvm.h"
#include "kvm/kvm_s390x.h"
#include "target/s390x/kvm/pv.h"
#include "qemu/error-report.h"
#include "crypto/x509-utils.h"


int handle_diag_288(CPUS390XState *env, uint64_t r1, uint64_t r3)
{
    uint64_t func = env->regs[r1];
    uint64_t timeout = env->regs[r1 + 1];
    uint64_t action = env->regs[r3];
    Object *obj;
    DIAG288State *diag288;
    DIAG288Class *diag288_class;

    if (r1 % 2 || action != 0) {
        return -1;
    }

    /* Timeout must be more than 15 seconds except for timer deletion */
    if (func != WDT_DIAG288_CANCEL && timeout < 15) {
        return -1;
    }

    obj = object_resolve_path_type("", TYPE_WDT_DIAG288, NULL);
    if (!obj) {
        return -1;
    }

    diag288 = DIAG288(obj);
    diag288_class = DIAG288_GET_CLASS(diag288);
    return diag288_class->handle_timer(diag288, func, timeout);
}

static int diag308_parm_check(CPUS390XState *env, uint64_t r1, uint64_t addr,
                              uintptr_t ra, bool write)
{
    /* Handled by the Ultravisor */
    if (s390_is_pv()) {
        return 0;
    }
    if ((r1 & 1) || (addr & ~TARGET_PAGE_MASK)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return -1;
    }
    if (!diag_parm_addr_valid(addr, sizeof(IplParameterBlock), write)) {
        s390_program_interrupt(env, PGM_ADDRESSING, ra);
        return -1;
    }
    return 0;
}

void handle_diag_308(CPUS390XState *env, uint64_t r1, uint64_t r3, uintptr_t ra)
{
    bool valid;
    CPUState *cs = env_cpu(env);
    S390CPU *cpu = env_archcpu(env);
    uint64_t addr =  env->regs[r1];
    uint64_t subcode = env->regs[r3];
    IplParameterBlock *iplb;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return;
    }

    if (subcode & ~0x0ffffULL) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    if (subcode >= DIAG308_PV_SET && !s390_has_feat(S390_FEAT_UNPACK)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    switch (subcode) {
    case DIAG308_RESET_MOD_CLR:
        s390_ipl_reset_request(cs, S390_RESET_MODIFIED_CLEAR);
        break;
    case DIAG308_RESET_LOAD_NORM:
        s390_ipl_reset_request(cs, S390_RESET_LOAD_NORMAL);
        break;
    case DIAG308_LOAD_CLEAR:
        /* Well we still lack the clearing bit... */
        s390_ipl_reset_request(cs, S390_RESET_REIPL);
        break;
    case DIAG308_SET:
    case DIAG308_PV_SET:
        if (diag308_parm_check(env, r1, addr, ra, false)) {
            return;
        }
        iplb = g_new0(IplParameterBlock, 1);
        if (!s390_is_pv()) {
            cpu_physical_memory_read(addr, iplb, sizeof(iplb->len));
        } else {
            s390_cpu_pv_mem_read(cpu, 0, iplb, sizeof(iplb->len));
        }

        if (!iplb_valid_len(iplb)) {
            env->regs[r1 + 1] = DIAG_308_RC_INVALID;
            goto out;
        }

        if (!s390_is_pv()) {
            cpu_physical_memory_read(addr, iplb, be32_to_cpu(iplb->len));
        } else {
            s390_cpu_pv_mem_read(cpu, 0, iplb, be32_to_cpu(iplb->len));
        }

        valid = subcode == DIAG308_PV_SET ? iplb_valid_pv(iplb) : iplb_valid(iplb);
        if (!valid) {
            if (subcode == DIAG308_SET && iplb->pbt == S390_IPL_TYPE_QEMU_SCSI) {
                s390_rebuild_iplb(iplb->devno, iplb);
                s390_ipl_update_diag308(iplb);
                env->regs[r1 + 1] = DIAG_308_RC_OK;
            } else {
                env->regs[r1 + 1] = DIAG_308_RC_INVALID;
            }

            goto out;
        }

        s390_ipl_update_diag308(iplb);
        env->regs[r1 + 1] = DIAG_308_RC_OK;
out:
        g_free(iplb);
        return;
    case DIAG308_STORE:
    case DIAG308_PV_STORE:
        if (diag308_parm_check(env, r1, addr, ra, true)) {
            return;
        }
        if (subcode == DIAG308_PV_STORE) {
            iplb = s390_ipl_get_iplb_pv();
        } else {
            iplb = s390_ipl_get_iplb();
        }
        if (!iplb) {
            env->regs[r1 + 1] = DIAG_308_RC_NO_CONF;
            return;
        }

        if (!s390_is_pv()) {
            cpu_physical_memory_write(addr, iplb, be32_to_cpu(iplb->len));
        } else {
            s390_cpu_pv_mem_write(cpu, 0, iplb, be32_to_cpu(iplb->len));
        }
        env->regs[r1 + 1] = DIAG_308_RC_OK;
        return;
    case DIAG308_PV_START:
        iplb = s390_ipl_get_iplb_pv();
        if (!iplb) {
            env->regs[r1 + 1] = DIAG_308_RC_NO_PV_CONF;
            return;
        }

        if (kvm_enabled() && kvm_s390_get_hpage_1m()) {
            error_report("Protected VMs can currently not be backed with "
                         "huge pages");
            env->regs[r1 + 1] = DIAG_308_RC_INVAL_FOR_PV;
            return;
        }

        s390_ipl_reset_request(cs, S390_RESET_PV);
        break;
    default:
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        break;
    }
}

static int diag_320_is_cert_valid(S390IPLCertificate qcert, Error **errp)
{
    int version;
    int rc;

    version = qcrypto_get_x509_cert_version((uint8_t *)qcert.raw, qcert.size, errp);
    if (version < 0) {
        return version == -ENOTSUP ? -ENOTSUP : 0;
    }

    rc = qcrypto_check_x509_cert_times((uint8_t *)qcert.raw, qcert.size, errp);
    if (rc) {
        return 0;
    }

    return 1;
}

static int diag_320_get_cert_info(VCEntry *vce, S390IPLCertificate qcert, int *is_valid,
                                  unsigned char **key_id_data, void **hash_data)
{
    int algo;
    int rc;
    Error *err = NULL;

    /* VCE flag (validity) */
    *is_valid = diag_320_is_cert_valid(qcert, &err);
    /* return early if GNUTLS is not enabled */
    if (*is_valid == -ENOTSUP) {
        error_report("GNUTLS is not supported");
        return -1;
    }

    /* key-type */
    algo = qcrypto_get_x509_pk_algorithm((uint8_t *)qcert.raw, qcert.size, &err);
    if (algo == QCRYPTO_PK_ALGO_RSA) {
        vce->key_type = DIAG_320_VCE_KEYTYPE_SELF_DESCRIBING;
    }

    /* VC format */
    if (qcert.format == QCRYPTO_CERT_FMT_DER) {
        vce->format = DIAG_320_VCE_FORMAT_X509_DER;
    }

    /* key id and key id len */
    *key_id_data = g_malloc0(qcert.key_id_size);
    rc = qcrypto_get_x509_cert_key_id((uint8_t *)qcert.raw, qcert.size,
                                      QCRYPTO_KEYID_FLAGS_SHA256,
                                      *key_id_data, &qcert.key_id_size, &err);
    if (rc < 0) {
        error_report("Fail to retrieve certificate key ID");
        goto out;
    }
    vce->keyid_len = cpu_to_be16(qcert.key_id_size);

    /* hash type */
    if (qcert.hash_type == QCRYPTO_SIG_ALGO_RSA_SHA256) {
        vce->hash_type = DIAG_320_VCE_HASHTYPE_SHA2_256;
    }

    /* hash and hash len */
    *hash_data = g_malloc0(qcert.hash_size);
    rc = qcrypto_get_x509_cert_fingerprint((uint8_t *)qcert.raw, qcert.size,
                                           QCRYPTO_HASH_ALGO_SHA256,
                                           *hash_data, &qcert.hash_size, &err);
    if (rc < 0) {
        error_report("Fail to retrieve certificate hash");
        goto out;
    }
    vce->hash_len = cpu_to_be16(qcert.hash_size);

    return 0;

out:
    g_free(*key_id_data);
    g_free(*hash_data);

    return -1;
}


void handle_diag_320(CPUS390XState *env, uint64_t r1, uint64_t r3, uintptr_t ra)
{
    S390CPU *cpu = env_archcpu(env);
    S390IPLCertificateStore *qcs = s390_ipl_get_certificate_store();
    uint64_t subcode = env->regs[r3];
    uint64_t addr = env->regs[r1];
    int rc;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return;
    }

    if (!s390_has_feat(S390_FEAT_DIAG_320)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    if (r1 & 1) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    switch (subcode) {
    case DIAG_320_SUBC_QUERY_ISM:
        uint64_t ism = cpu_to_be64(DIAG_320_ISM_QUERY_VCSI | DIAG_320_ISM_STORE_VC);

        if (s390_cpu_virt_mem_write(cpu, addr, r1, &ism, sizeof(ism))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }

        rc = DIAG_320_RC_OK;
        break;
    case DIAG_320_SUBC_QUERY_VCSI:
        VCStorageSizeBlock vcssb;

        if (!diag_parm_addr_valid(addr, sizeof(VCStorageSizeBlock),
                                  true)) {
            s390_program_interrupt(env, PGM_ADDRESSING, ra);
            return;
        }

        if (!qcs || !qcs->count) {
            vcssb.length = 4;
        } else {
            vcssb.length = cpu_to_be32(VCSSB_MAX_LEN);
            vcssb.version = 0;
            vcssb.total_vc_ct = cpu_to_be16(qcs->count);
            vcssb.max_vc_ct = cpu_to_be16(MAX_CERTIFICATES);
            vcssb.max_vce_len = cpu_to_be32(VCE_HEADER_LEN + qcs->max_cert_size);
            vcssb.max_single_vcb_len = cpu_to_be32(VCB_HEADER_LEN + VCE_HEADER_LEN +
                                                   qcs->max_cert_size);
            vcssb.total_vcb_len = cpu_to_be32(VCB_HEADER_LEN +
                                              qcs->count * VCE_HEADER_LEN +
                                              qcs->total_bytes);
        }

        if (be32_to_cpu(vcssb.length) > 4 && be32_to_cpu(vcssb.length) < 128) {
            rc = DIAG_320_RC_INVAL_VCSSB_LEN;
            break;
        }

        if (s390_cpu_virt_mem_write(cpu, addr, r1, &vcssb, sizeof(VCStorageSizeBlock))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }
        rc = DIAG_320_RC_OK;
        break;
    case DIAG_320_SUBC_STORE_VC:
        VCBlock *vcb;
        size_t vce_offset;
        size_t remaining_space;
        size_t keyid_buf_size;
        size_t hash_buf_size;
        size_t cert_buf_size;
        uint32_t vce_len;
        unsigned char *key_id_data = NULL;
        void *hash_data = NULL;
        int is_valid = 0;
        uint16_t first_vc_index;
        uint16_t last_vc_index;
        uint32_t in_len;

        vcb = g_new0(VCBlock, 1);
        if (s390_cpu_virt_mem_read(cpu, addr, r1, vcb, sizeof(*vcb))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }

        in_len = be32_to_cpu(vcb->in_len);
        first_vc_index = be16_to_cpu(vcb->first_vc_index);
        last_vc_index = be16_to_cpu(vcb->last_vc_index);

        if (in_len % TARGET_PAGE_SIZE != 0) {
            rc = DIAG_320_RC_INVAL_VCB_LEN;
            g_free(vcb);
            break;
        }

        if (first_vc_index > last_vc_index) {
            rc = DIAG_320_RC_BAD_RANGE;
            g_free(vcb);
            break;
        }

        if (first_vc_index == 0) {
            /*
             * Zero is a valid index for the first and last VC index.
             * Zero index results in the VCB header and zero certificates returned.
             */
            if (last_vc_index == 0) {
                goto out;
            }

            /* DIAG320 certificate store remains a one origin for cert entries */
            vcb->first_vc_index = 1;
        }

        vce_offset = VCB_HEADER_LEN;
        vcb->out_len = VCB_HEADER_LEN;
        remaining_space = in_len - VCB_HEADER_LEN;

        for (int i = first_vc_index - 1; i < last_vc_index && i < qcs->count; i++) {
            VCEntry *vce;
            S390IPLCertificate qcert = qcs->certs[i];
            /*
             * Each VCE is word aligned.
             * Each variable length field within the VCE is also word aligned.
             */
            keyid_buf_size = ROUND_UP(qcert.key_id_size, 4);
            hash_buf_size = ROUND_UP(qcert.hash_size, 4);
            cert_buf_size = ROUND_UP(qcert.size, 4);
            vce_len = VCE_HEADER_LEN + cert_buf_size + keyid_buf_size + hash_buf_size;

            /*
             * If there is no more space to store the cert,
             * set the remaining verification cert count and
             * break early.
             */
            if (remaining_space < vce_len) {
                vcb->remain_ct = cpu_to_be16(last_vc_index - i);
                break;
            }

            /*
             * Construct VCE
             * Unused area following the VCE field contains zeros.
             */
            vce = g_malloc0(vce_len);
            vce->len = cpu_to_be32(vce_len);
            vce->cert_idx = cpu_to_be16(i + 1);
            vce->cert_len = cpu_to_be32(qcert.size);

            strncpy((char *)vce->name, (char *)qcert.vc_name, VC_NAME_LEN_BYTES);

            rc = diag_320_get_cert_info(vce, qcert, &is_valid, &key_id_data, &hash_data);
            if (rc) {
                g_free(vce);
                continue;
            }

            /* VCE field offset is also word aligned */
            vce->hash_offset = cpu_to_be16(VCE_HEADER_LEN + keyid_buf_size);
            vce->cert_offset = cpu_to_be16(VCE_HEADER_LEN + keyid_buf_size +
                                           hash_buf_size);

            /* Write Key ID */
            memcpy(vce->cert_buf, key_id_data, qcert.key_id_size);
            /* Write Hash key */
            memcpy(vce->cert_buf + keyid_buf_size, hash_data, qcert.hash_size);
            /* Write VCE cert data */
            memcpy(vce->cert_buf + keyid_buf_size + hash_buf_size, qcert.raw, qcert.size);

            /* The certificate is valid and VCE contains the certificate */
            if (is_valid) {
                vce->flags |= DIAG_320_VCE_FLAGS_VALID;
            }

            /* Write VCE Header */
            if (s390_cpu_virt_mem_write(cpu, addr + vce_offset, r1, vce, vce_len)) {
                s390_cpu_virt_mem_handle_exc(cpu, ra);
                return;
            }

            vce_offset += vce_len;
            vcb->out_len += vce_len;
            remaining_space -= vce_len;
            vcb->stored_ct++;

            g_free(vce);
            g_free(key_id_data);
            g_free(hash_data);
        }

        vcb->out_len = cpu_to_be32(vcb->out_len);
        vcb->stored_ct = cpu_to_be16(vcb->stored_ct);

    out:
        /*
         * Write VCB header
         * All VCEs have been populated with the latest information
         * and write VCB header last.
         */
        if (s390_cpu_virt_mem_write(cpu, addr, r1, vcb, VCB_HEADER_LEN)) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }
        rc = DIAG_320_RC_OK;
        g_free(vcb);
        break;
    default:
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }
    env->regs[r1 + 1] = rc;
}

void handle_diag_508(CPUS390XState *env, uint64_t r1, uint64_t r3, uintptr_t ra)
{
    uint64_t subcode = env->regs[r3];
    int rc;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return;
    }

    if ((subcode & ~0x0ffffULL) || (r1 & 1)) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    switch (subcode) {
    case DIAG_508_SUBC_QUERY_SUBC:
        rc = 0;
        break;
    default:
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }
    env->regs[r1 + 1] = rc;
}
