/*
 * S/390 Secure IPL
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>
#include <stdio.h>
#include "bootmap.h"
#include "s390-ccw.h"
#include "secure-ipl.h"

uint8_t vcb_data[MAX_SECTOR_SIZE * 4] __attribute__((__aligned__(PAGE_SIZE)));
uint8_t vcssb_data[VCSSB_MAX_LEN] __attribute__((__aligned__(PAGE_SIZE)));

VCStorageSizeBlock *zipl_secure_get_vcssb(void)
{
    VCStorageSizeBlock *vcssb;
    int rc;

    vcssb = (VCStorageSizeBlock *)vcssb_data;
    /* avoid retrieving vcssb multiple times */
    if (vcssb->length == VCSSB_MAX_LEN) {
        return vcssb;
    }

    rc = diag320(vcssb, DIAG_320_SUBC_QUERY_VCSI);
    if (rc != DIAG_320_RC_OK) {
        return NULL;
    }

    return vcssb;
}

uint32_t zipl_secure_request_certificate(uint64_t *cert, uint8_t index)
{
    VCStorageSizeBlock *vcssb;
    VCBlock *vcb;
    VCEntry *vce;
    uint64_t rc = 0;
    uint32_t cert_len = 0;

    /* Get Verification Certificate Storage Size block with DIAG320 subcode 1 */
    vcssb = zipl_secure_get_vcssb();
    if (vcssb == NULL) {
        return 0;
    }

    /*
     * Request single entry
     * Fill input fields of single-entry VCB
     */
    vcb = (VCBlock *)vcb_data;
    vcb->in_len = ROUND_UP(vcssb->max_single_vcb_len, PAGE_SIZE);
    vcb->first_vc_index = index + 1;
    vcb->last_vc_index = index + 1;

    rc = diag320(vcb, DIAG_320_SUBC_STORE_VC);
    if (rc == DIAG_320_RC_OK) {
        vce = (VCEntry *)vcb->vce_buf;
        cert_len = vce->cert_len;
        memcpy(cert, (uint8_t *)vce + vce->cert_offset, vce->cert_len);
        /* clear out region for next cert(s) */
        memcpy(vcb_data, 0, sizeof(vcb_data));
    }

    return cert_len;
}

void zipl_secure_cert_list_add(IplSignatureCertificateList *certs, int cert_index,
                               uint64_t *cert, uint64_t cert_len)
{
    if (cert_index > MAX_CERTIFICATES - 1) {
        printf("Warning: Ignoring cert entry [%d] because it's over %d entires\n",
                cert_index + 1, MAX_CERTIFICATES);
        return;
    }

    certs->cert_entries[cert_index].addr = (uint64_t)cert;
    certs->cert_entries[cert_index].len = cert_len;
    certs->ipl_info_header.len += sizeof(certs->cert_entries[cert_index]);
}

void zipl_secure_comp_list_add(IplDeviceComponentList *comps, int comp_index,
                               int cert_index, uint64_t comp_addr,
                               uint64_t comp_len, uint8_t flags)
{
    if (comp_index > MAX_CERTIFICATES - 1) {
        printf("Warning: Ignoring comp entry [%d] because it's over %d entires\n",
                comp_index + 1, MAX_CERTIFICATES);
        return;
    }

    comps->device_entries[comp_index].addr = comp_addr;
    comps->device_entries[comp_index].len = comp_len;
    comps->device_entries[comp_index].flags = flags;
    comps->device_entries[comp_index].cert_index = cert_index;
    comps->ipl_info_header.len += sizeof(comps->device_entries[comp_index]);
}

int zipl_secure_update_iirb(IplDeviceComponentList *comps,
                            IplSignatureCertificateList *certs)
{
    IplInfoReportBlock *iirb;
    IplDeviceComponentList *iirb_comps;
    IplSignatureCertificateList *iirb_certs;
    uint32_t iirb_hdr_len;
    uint32_t comps_len;
    uint32_t certs_len;

    if (iplb->len % 8 != 0) {
        panic("IPL parameter block length field value is not multiple of 8 bytes");
    }

    iirb_hdr_len = sizeof(IplInfoReportBlockHeader);
    comps_len = comps->ipl_info_header.len;
    certs_len = certs->ipl_info_header.len;
    if ((comps_len + certs_len + iirb_hdr_len) > sizeof(IplInfoReportBlock)) {
        puts("Not enough space to hold all components and certificates in IIRB");
        return -1;
    }

    /* IIRB immediately follows IPLB */
    iirb = &ipl_data.iirb;
    iirb->hdr.len = iirb_hdr_len;

    /* Copy IPL device component list after IIRB Header */
    iirb_comps = (IplDeviceComponentList *) iirb->info_blks;
    memcpy(iirb_comps, comps, comps_len);

    /* Update IIRB length */
    iirb->hdr.len += comps_len;

    /* Copy IPL sig cert list after IPL device component list */
    iirb_certs = (IplSignatureCertificateList *) (iirb->info_blks +
                                                  iirb_comps->ipl_info_header.len);
    memcpy(iirb_certs, certs, certs_len);

    /* Update IIRB length */
    iirb->hdr.len += certs_len;

    return 0;
}

bool zipl_secure_ipl_supported(void)
{
    if (!sclp_is_sipl_on()) {
        puts("Secure IPL Facility is not supported by the hypervisor!");
        return false;
    }

    if (!is_secure_ipl_extension_supported()) {
        puts("Secure IPL extensions are not supported by the hypervisor!");
        return false;
    }

    if (!(sclp_is_diag320_on() && is_cert_store_facility_supported())) {
        puts("Certificate Store Facility is not supported by the hypervisor!");
        return false;
    }

    if (!sclp_is_sclaf_on()) {
        puts("Secure IPL Code Loading Attributes Facility is not supported by" \
             " the hypervisor!");
        return false;
    }

    return true;
}

void zipl_secure_init_lists(IplDeviceComponentList *comps,
                            IplSignatureCertificateList *certs)
{
    comps->ipl_info_header.ibt = IPL_IBT_COMPONENTS;
    comps->ipl_info_header.len = sizeof(comps->ipl_info_header);

    certs->ipl_info_header.ibt = IPL_IBT_CERTIFICATES;
    certs->ipl_info_header.len = sizeof(certs->ipl_info_header);
}

static bool is_comp_overlap(SecureIplCompAddrRange *comp_addr_range, int addr_range_index,
                            uint64_t start_addr, uint64_t end_addr)
{
    /* neither a signed nor an unsigned component can overlap with a signed component */
    for (int i = 0; i < addr_range_index; i++) {
        if ((comp_addr_range[i].start_addr <= end_addr &&
            start_addr <= comp_addr_range[i].end_addr) &&
            comp_addr_range[i].is_signed) {
            return true;
       }
    }

    return false;
}

static void comp_addr_range_add(SecureIplCompAddrRange *comp_addr_range,
                                int addr_range_index, bool is_signed,
                                uint64_t start_addr, uint64_t end_addr)
{
    comp_addr_range[addr_range_index].is_signed = is_signed;
    comp_addr_range[addr_range_index].start_addr = start_addr;
    comp_addr_range[addr_range_index].end_addr = end_addr;
}

static void unsigned_addr_check(uint64_t load_addr, IplDeviceComponentList *comps,
                                int comp_index)
{
    bool is_addr_valid;

    is_addr_valid = load_addr >= 0x2000;
    if (!is_addr_valid) {
        comps->device_entries[comp_index].cei |=
        S390_IPL_COMPONENT_CEI_INVALID_UNSIGNED_ADDR;
        zipl_secure_print_func(is_addr_valid, "Load address is less than 0x2000");
    }
}

void zipl_secure_addr_overlap_check(SecureIplCompAddrRange *comp_addr_range,
                                    int *addr_range_index,
                                    uint64_t start_addr, uint64_t end_addr,
                                    bool is_signed)
{
    bool overlap;

    overlap = is_comp_overlap(comp_addr_range, *addr_range_index,
                              start_addr, end_addr);
    if (!overlap) {
        comp_addr_range_add(comp_addr_range, *addr_range_index, is_signed,
                            start_addr, end_addr);
        *addr_range_index += 1;
    } else {
        zipl_secure_print_func(!overlap, "Component addresses overlap");
    }
}

static void valid_sclab_check(SclabOriginLocator *sclab_locator,
                              IplDeviceComponentList *comps, int comp_index)
{
    bool is_magic_match;
    bool is_len_valid;

    /* identifies the presence of SCLAB */
    is_magic_match = magic_match(sclab_locator->magic, ZIPL_MAGIC);
    if (!is_magic_match) {
        comps->device_entries[comp_index].cei |= S390_IPL_COMPONENT_CEI_INVALID_SCLAB;

        /* a missing SCLAB will not be reported in audit mode */
        return;
    }

    is_len_valid = sclab_locator->len >= 32;
    if (!is_len_valid) {
        comps->device_entries[comp_index].cei |= S390_IPL_COMPONENT_CEI_INVALID_SCLAB_LEN;
        comps->device_entries[comp_index].cei |= S390_IPL_COMPONENT_CEI_INVALID_SCLAB;
        zipl_secure_print_func(is_len_valid, "Invalid SCLAB length");
    }
}

static void sclab_format_check(SecureCodeLoadingAttributesBlock *sclab,
                               IplDeviceComponentList *comps, int comp_index)
{
    bool valid_format;

    valid_format = sclab->format == 0;
    if (!valid_format) {
        comps->device_entries[comp_index].cei |=
        S390_IPL_COMPONENT_CEI_INVALID_SCLAB_FORMAT;
    }
    zipl_secure_print_func(valid_format, "Format-0 SCLAB is not being used");
}

static void sclab_opsw_check(SecureCodeLoadingAttributesBlock *sclab,
                             int *global_sclab_count, uint64_t *sclab_load_psw,
                             IplDeviceComponentList *comps, int comp_index)
{
    bool is_load_psw_zero;
    bool is_ola_on;
    bool has_one_glob_sclab;

    /* OPSW is zero */
    if (!(sclab->flags & S390_IPL_SCLAB_FLAG_OPSW)) {
        is_load_psw_zero = sclab->load_psw == 0;
        if (!is_load_psw_zero) {
            comps->device_entries[comp_index].cei |=
            S390_IPL_COMPONENT_CEI_SCLAB_LOAD_PSW_NOT_ZERO;
            zipl_secure_print_func(is_load_psw_zero,
                       "Load PSW is not zero when Override PSW bit is zero");
        }
    } else {
        is_ola_on = sclab->flags & S390_IPL_SCLAB_FLAG_OLA;
        if (!is_ola_on) {
            comps->device_entries[comp_index].cei |=
            S390_IPL_COMPONENT_CEI_SCLAB_OLA_NOT_ONE;
            zipl_secure_print_func(is_ola_on,
                       "Override Load Address bit is not set to one in the global SCLAB");
        }

        *global_sclab_count += 1;
        if (*global_sclab_count == 1) {
            *sclab_load_psw = sclab->load_psw;
        } else {
            has_one_glob_sclab = false;
            comps->ipl_info_header.iiei |= S390_IPL_INFO_IIEI_MORE_GLOBAL_SCLAB;
            zipl_secure_print_func(has_one_glob_sclab, "More than one global SCLAB");
        }
    }
}

static void sclab_ola_check(SecureCodeLoadingAttributesBlock *sclab,
                            uint64_t load_addr, IplDeviceComponentList *comps,
                            int comp_index)
{
    bool is_load_addr_zero;
    bool is_matched;

    /* OLA is zero */
    if (!(sclab->flags & S390_IPL_SCLAB_FLAG_OLA)) {
        is_load_addr_zero = sclab->load_addr == 0;
        if (!is_load_addr_zero) {
            comps->device_entries[comp_index].cei |=
            S390_IPL_COMPONENT_CEI_SCLAB_LOAD_ADDR_NOT_ZERO;
            zipl_secure_print_func(is_load_addr_zero,
                       "Load Address is not zero when Override Load Address bit is zero");
        }
    } else {
        is_matched = sclab->load_addr == load_addr;
        if (!is_matched) {
            comps->device_entries[comp_index].cei |=
            S390_IPL_COMPONENT_CEI_UNMATCHED_SCLAB_LOAD_ADDR;
            zipl_secure_print_func(is_matched,
                       "Load Address does not match with component load address");
        }
    }
}

static bool is_psw_valid(uint64_t psw, SecureIplCompAddrRange *comp_addr_range,
                         int range_index)
{
    uint32_t addr = psw & 0x3FFFFFFF;

    /* PSW points to the beginning of a signed binary code component */
    for (int i = 0; i < range_index; i++) {
        if (comp_addr_range[i].is_signed && comp_addr_range[i].start_addr == addr) {
            return true;
       }
    }

    return false;
}

void zipl_secure_load_psw_check(SecureIplCompAddrRange *comp_addr_range,
                                int addr_range_index, uint64_t sclab_load_psw,
                                uint64_t load_psw, IplDeviceComponentList *comps,
                                int comp_index)
{
    bool is_valid;
    bool is_matched;

    is_valid = is_psw_valid(sclab_load_psw, comp_addr_range, addr_range_index) &&
               is_psw_valid(load_psw, comp_addr_range, addr_range_index);
    if (!is_valid) {
        comps->device_entries[comp_index].cei |= S390_IPL_COMPONENT_CEI_INVALID_LOAD_PSW;
        zipl_secure_print_func(is_valid, "Invalid PSW");
    }

    is_matched = load_psw == sclab_load_psw;
    if (!is_matched) {
        comps->device_entries[comp_index].cei |=
        S390_IPL_COMPONENT_CEI_UNMATCHED_SCLAB_LOAD_PSW;
        zipl_secure_print_func(is_matched,
                               "Load PSW does not match with PSW in component");
    }
}

void zipl_secure_check_unsigned_comp(uint64_t comp_addr, IplDeviceComponentList *comps,
                                     int comp_index, int cert_index, uint64_t comp_len)
{
    unsigned_addr_check(comp_addr, comps, comp_index);

    zipl_secure_comp_list_add(comps, comp_index, cert_index, comp_addr, comp_len, 0x00);
}

void zipl_secure_check_sclab(uint64_t comp_addr, IplDeviceComponentList *comps,
                             uint64_t comp_len, int comp_index, int *sclab_count,
                             uint64_t *sclab_load_psw, int *global_sclab_count)
{
    SclabOriginLocator *sclab_locator;
    SecureCodeLoadingAttributesBlock *sclab;

    sclab_locator = (SclabOriginLocator *)(comp_addr + comp_len - 8);
    valid_sclab_check(sclab_locator, comps, comp_index);

    if ((comps->device_entries[comp_index].cei &
         S390_IPL_COMPONENT_CEI_INVALID_SCLAB) == 0) {
        *sclab_count += 1;
        sclab = (SecureCodeLoadingAttributesBlock *)(comp_addr + comp_len -
                                                     sclab_locator->len);

        sclab_format_check(sclab, comps, comp_index);
        sclab_opsw_check(sclab, global_sclab_count, sclab_load_psw,
                         comps, comp_index);
        sclab_ola_check(sclab, comp_addr, comps, comp_index);
    }
}
