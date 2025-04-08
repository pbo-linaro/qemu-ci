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

#ifdef CONFIG_GNUTLS
#include <gnutls/x509.h>
#include <gnutls/gnutls.h>
#include <gnutls/pkcs7.h>
#endif /* CONFIG_GNUTLS */

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

#ifdef CONFIG_GNUTLS
static bool diag_320_is_cert_valid(gnutls_x509_crt_t cert)
{
    time_t now;

    if (gnutls_x509_crt_get_version(cert) < 0) {
        return false;
    }

    now = time(0);
    if (!((gnutls_x509_crt_get_activation_time(cert) < now) &&
         (gnutls_x509_crt_get_expiration_time(cert) > now))) {
        return false;
    }

    return true;
}
#endif /* CONFIG_GNUTLS */

static int diag_320_get_cert_info(VerificationCertificateEntry *vce,
                                 S390IPLCertificate qcert, bool *is_valid,
                                 unsigned char **key_id_data, void **hash_data)
{
#ifdef CONFIG_GNUTLS
    unsigned int algo;
    unsigned int bits;
    int hash_type;
    int rc;

    gnutls_x509_crt_t g_cert = NULL;
    if (g_init_cert((uint8_t *)qcert.raw, qcert.size, &g_cert)) {
        return -1;
    }

    /* VCE flag (validity) */
    *is_valid = diag_320_is_cert_valid(g_cert);

    /* key-type */
    algo = gnutls_x509_crt_get_pk_algorithm(g_cert, &bits);
    if (algo == GNUTLS_PK_RSA) {
        vce->vce_hdr.keytype = DIAG_320_VCE_KEYTYPE_SELF_DESCRIBING;
    }

    /* VC format */
    if (qcert.format == GNUTLS_X509_FMT_DER) {
        vce->vce_hdr.format = DIAG_320_VCE_FORMAT_X509_DER;
    }

    /* key id and key id len */
    *key_id_data = g_malloc0(qcert.key_id_size);
    rc = gnutls_x509_crt_get_key_id(g_cert, GNUTLS_KEYID_USE_SHA256,
                                    *key_id_data, &qcert.key_id_size);
    if (rc < 0) {
        error_report("Fail to retrieve certificate key ID");
        goto out;
    }
    vce->vce_hdr.keyidlen = (uint16_t)qcert.key_id_size;

    /* hash type */
    hash_type = gnutls_x509_crt_get_signature_algorithm(g_cert);
    if (hash_type == GNUTLS_SIGN_RSA_SHA256) {
        vce->vce_hdr.hashtype = DIAG_320_VCE_HASHTYPE_SHA2_256;
    }

    /* hash and hash len */
    *hash_data = g_malloc0(qcert.hash_size);
    rc = gnutls_x509_crt_get_fingerprint(g_cert, GNUTLS_DIG_SHA256,
                                            *hash_data, &qcert.hash_size);
    if (rc < 0) {
        error_report("Fail to retrieve certificate hash");
        goto out;
    }
    vce->vce_hdr.hashlen = (uint16_t)qcert.hash_size;

    gnutls_x509_crt_deinit(g_cert);

    return 0;
out:
    gnutls_x509_crt_deinit(g_cert);
    g_free(*key_id_data);
    g_free(*hash_data);

    return -1;
#else
    return -1;
#endif /* CONFIG_GNUTLS */
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

    if (r1 & 1) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    switch (subcode) {
    case DIAG_320_SUBC_QUERY_ISM:
        uint64_t ism = DIAG_320_ISM_QUERY_VCSI | DIAG_320_ISM_STORE_VC;

        if (s390_cpu_virt_mem_write(cpu, addr, (uint8_t)r1, &ism,
                                    be64_to_cpu(sizeof(ism)))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }

        rc = DIAG_320_RC_OK;
        break;
    case DIAG_320_SUBC_QUERY_VCSI:
        VerificationCertificateStorageSizeBlock vcssb;

        if (!diag_parm_addr_valid(addr, sizeof(VerificationCertificateStorageSizeBlock),
                                  true)) {
            s390_program_interrupt(env, PGM_ADDRESSING, ra);
            return;
        }

        if (!qcs || !qcs->count) {
            vcssb.length = 4;
        } else {
            vcssb.length = VCSSB_MAX_LEN;
            vcssb.version = 0;
            vcssb.totalvc = qcs->count;
            vcssb.maxvc = MAX_CERTIFICATES;
            vcssb.maxvcelen = VCE_HEADER_LEN + qcs->max_cert_size;
            vcssb.largestvcblen = VCB_HEADER_LEN + vcssb.maxvcelen;
            vcssb.totalvcblen = VCB_HEADER_LEN + qcs->count * VCE_HEADER_LEN +
                                qcs->total_bytes;
        }

        if (vcssb.length < 128) {
            rc = DIAG_320_RC_NOMEM;
            break;
        }

        if (s390_cpu_virt_mem_write(cpu, addr, (uint8_t)r1, &vcssb,
                                    be64_to_cpu(
                                        sizeof(VerificationCertificateStorageSizeBlock)
                                    ))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }
        rc = DIAG_320_RC_OK;
        break;
    case DIAG_320_SUBC_STORE_VC:
        VerficationCertificateBlock *vcb;
        size_t vce_offset = VCB_HEADER_LEN;
        size_t remaining_space;
        size_t vce_hdr_offset;
        int i;

        unsigned char *key_id_data = NULL;
        void *hash_data = NULL;
        bool is_valid = false;

        vcb = g_new0(VerficationCertificateBlock, 1);
        if (s390_cpu_virt_mem_read(cpu, addr, (uint8_t)r1, vcb, sizeof(*vcb))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return;
        }

        if (vcb->vcb_hdr.vcbinlen % 4096 != 0) {
            rc = DIAG_320_RC_INVAL_VCB_LEN;
            g_free(vcb);
            break;
        }

        if (1 > vcb->vcb_hdr.fvci || vcb->vcb_hdr.fvci > vcb->vcb_hdr.lvci) {
            rc = DIAG_320_RC_BAD_RANGE;
            g_free(vcb);
            break;
        }

        vcb->vcb_hdr.vcboutlen = VCB_HEADER_LEN;
        vcb->vcb_hdr.version = 0;
        vcb->vcb_hdr.svcc = 0;
        vcb->vcb_hdr.rvcc = 0;

        remaining_space = vcb->vcb_hdr.vcbinlen - VCB_HEADER_LEN;

        for (i = vcb->vcb_hdr.fvci - 1; i < vcb->vcb_hdr.lvci; i++) {
            VerificationCertificateEntry vce;
            S390IPLCertificate qcert;

            /*
             * If cert index goes beyond the highest cert
             * store index (count - 1), then exit early
             */
            if (i >= qcs->count) {
                break;
            }

            qcert = qcs->certs[i];

            /*
             * If there is no more space to store the cert,
             * set the remaining verification cert count and
             * break early.
             */
            if (remaining_space < qcert.size) {
                vcb->vcb_hdr.rvcc = vcb->vcb_hdr.lvci - i;
                break;
            }

            /* Construct VCE */
            vce.vce_hdr.len = VCE_HEADER_LEN;
            vce.vce_hdr.certidx = i + 1;
            vce.vce_hdr.certlen = qcert.size;

            strncpy((char *)vce.vce_hdr.name, (char *)qcert.vc_name, VC_NAME_LEN_BYTES);

            rc = diag_320_get_cert_info(&vce, qcert, &is_valid, &key_id_data, &hash_data);
            if (rc) {
                continue;
            }

            vce.vce_hdr.len += vce.vce_hdr.keyidlen;
            vce.vce_hdr.len += vce.vce_hdr.hashlen;
            vce.vce_hdr.len += vce.vce_hdr.certlen;

            vce.vce_hdr.hashoffset = VCE_HEADER_LEN + vce.vce_hdr.keyidlen;
            vce.vce_hdr.certoffset = VCE_HEADER_LEN + vce.vce_hdr.keyidlen +
                                     vce.vce_hdr.hashlen;

            vce_hdr_offset = vce_offset;
            vce_offset += VCE_HEADER_LEN;

            /* Write Key ID */
            if (s390_cpu_virt_mem_write(cpu, addr + vce_offset, (uint8_t)r1, key_id_data,
                                        be16_to_cpu(vce.vce_hdr.keyidlen))) {
                s390_cpu_virt_mem_handle_exc(cpu, ra);
                return;
            }
            vce_offset += vce.vce_hdr.keyidlen;

            /* Write Hash key */
            if (s390_cpu_virt_mem_write(cpu, addr + vce_offset, (uint8_t)r1, hash_data,
                                        be16_to_cpu(vce.vce_hdr.hashlen))) {
                s390_cpu_virt_mem_handle_exc(cpu, ra);
                return;
            }
             vce_offset += vce.vce_hdr.hashlen;

            /* Write VCE cert data */
            if (s390_cpu_virt_mem_write(cpu, addr + vce_offset, (uint8_t)r1, qcert.raw,
                                        be32_to_cpu(vce.vce_hdr.certlen))) {
                s390_cpu_virt_mem_handle_exc(cpu, ra);
                return;
            }
            vce_offset += qcert.size;

            /* The certificate is valid and VCE contains the certificate */
            if (is_valid) {
                vce.vce_hdr.flags |= DIAG_320_VCE_FLAGS_VALID;
            }

            /* Write VCE Header */
            if (s390_cpu_virt_mem_write(cpu, addr + vce_hdr_offset, (uint8_t)r1, &vce,
                                        be32_to_cpu(VCE_HEADER_LEN))) {
                s390_cpu_virt_mem_handle_exc(cpu, ra);
                return;
            }

            vcb->vcb_hdr.vcboutlen += vce.vce_hdr.len;
            remaining_space -= vce.vce_hdr.len;
            vcb->vcb_hdr.svcc++;

            g_free(key_id_data);
            g_free(hash_data);
        }

        /* Finally, write the header */
        if (s390_cpu_virt_mem_write(cpu, addr, (uint8_t)r1, vcb,
                                    be32_to_cpu(VCB_HEADER_LEN))) {
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

#ifdef CONFIG_GNUTLS
#define datum_init(datum, data, size) \
    datum = (gnutls_datum_t){data, size}

static int diag_508_init_comp(gnutls_datum_t *comp,
                              Diag508SignatureVerificationBlock *svb)
{
    uint8_t *svb_comp = NULL;

    if (!svb->comp_len || !svb->comp_addr) {
        error_report("No component data.");
        return -1;
    }

    /*
     * corrupted size vs. prev_size in fastbins, occurs during 2nd iteration,
     * allocating 1mil bytes.
     */
    svb_comp = g_malloc0(svb->comp_len);
    cpu_physical_memory_read(svb->comp_addr, svb_comp, svb->comp_len);

    /*
     * Component data is not written back to the caller,
     * so no need to do a deep copy. Comp is freed when
     * svb is freed.
     */
    datum_init(*comp, svb_comp, svb->comp_len);
    return 0;
}

static int diag_508_init_signature(gnutls_pkcs7_t *sig,
                                   Diag508SignatureVerificationBlock *svb)
{
    gnutls_datum_t datum_sig;
    uint8_t *svb_sig = NULL;

    if (!svb->sig_len || !svb->sig_addr) {
        error_report("No signature data");
        return -1;
    }

    svb_sig = g_malloc0(svb->sig_len);
    cpu_physical_memory_read(svb->sig_addr, svb_sig, svb->sig_len);

    if (gnutls_pkcs7_init(sig) < 0) {
        error_report("Failed to initalize pkcs7 data.");
        return -1;
    }

    datum_init(datum_sig, svb_sig, svb->sig_len);
    return gnutls_pkcs7_import(*sig, &datum_sig, GNUTLS_X509_FMT_DER);

}
#endif /* CONFIG_GNUTLS */

void handle_diag_508(CPUS390XState *env, uint64_t r1, uint64_t r3, uintptr_t ra)
{
    S390IPLCertificateStore *qcs = s390_ipl_get_certificate_store();
    size_t csi_size = sizeof(Diag508CertificateStoreInfo);
    uint64_t subcode = env->regs[r3];
    uint64_t addr = env->regs[r1];
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
        rc = DIAG_508_SUBC_SIG_VERIF;
        break;
    case DIAG_508_SUBC_SIG_VERIF:
        size_t svb_size = sizeof(Diag508SignatureVerificationBlock);
        Diag508SignatureVerificationBlock *svb;

        if (!qcs || !qcs->count) {
            error_report("No certificates in cert store.");
            rc = DIAG_508_RC_NO_CERTS;
            break;
        }

        if (!diag_parm_addr_valid(addr, svb_size, false) ||
            !diag_parm_addr_valid(addr, csi_size, true)) {
            s390_program_interrupt(env, PGM_ADDRESSING, ra);
            return;
        }

        svb = g_new0(Diag508SignatureVerificationBlock, 1);
        cpu_physical_memory_read(addr, svb, svb_size);

#ifdef CONFIG_GNUTLS
        gnutls_pkcs7_t sig = NULL;
        gnutls_datum_t comp;
        int i;

        if (diag_508_init_comp(&comp, svb) < 0) {
            rc = DIAG_508_RC_INVAL_COMP_DATA;
            g_free(svb);
            break;
        }

        if (diag_508_init_signature(&sig, svb) < 0) {
            rc = DIAG_508_RC_INVAL_PKCS7_SIG;
            gnutls_pkcs7_deinit(sig);
            g_free(svb);
            break;
        }

        rc = DIAG_508_RC_FAIL_VERIF;
        /*
         * It is uncertain which certificate contains
         * the analogous key to verify the signed data
         */
        for (i = 0; i < qcs->count; i++) {
            gnutls_x509_crt_t g_cert = NULL;
            if (g_init_cert((uint8_t *)qcs->certs[i].raw, qcs->certs[i].size, &g_cert)) {
                continue;
            }

            if (gnutls_pkcs7_verify_direct(sig, g_cert, 0, &comp, 0) == 0) {
                svb->csi.idx = i;
                svb->csi.len = qcs->certs[i].size;
                cpu_physical_memory_write(addr, &svb->csi,
                                          be32_to_cpu(csi_size));
                rc = DIAG_508_RC_OK;
                break;
            }

            gnutls_x509_crt_deinit(g_cert);
        }

        gnutls_pkcs7_deinit(sig);
#else
        rc = DIAG_508_RC_FAIL_VERIF;
#endif /* CONFIG_GNUTLS */
        g_free(svb);
        break;
    default:
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }
    env->regs[r1 + 1] = rc;
}
