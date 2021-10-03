//
//  SoftwareUpdate.cpp
//  RestrictEvents
//
//  Copyright © 2021 vit9696. All rights reserved.
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_user.hpp>

/**
 
 Non-Apple hardware or unsupported Apple hardware often has to be spoofed to gibraltar models to support new hardware and macOS versions.
 For example, MacPro5,1 becomes MacPro7,1 or some AMI machine can become iMacPro1,1:
 
 {
  'AllowSameBuildVersion': 'false',
  'AllowSameRestoreVersion': 'false',
  'AssetAudience': '...',
  'AssetType': 'com.apple.MobileAsset.MacSoftwareUpdate',
  'BaseUrl': 'https://mesu.apple.com/assets/macos/',
  'BuildVersion': '21A5534d',
  'ClientData': {'AllowXmlFallback': 'false', 'DeviceAccessClient': 'softwareupdated'},
  'ClientVersion': 2,
  'DelayRequested': 'false',
  'DeviceCheck': 'Background',
  'DeviceName': 'Mac',
  'DeviceOSData': {},
  'HWModelStr': 'Mac-27AD2F918AE68F61',
  'InternalBuild': 'false',
  'NoFallback': 'true',
  'Nonce': '...',
  'ProductType': 'MacPro5,1',
  'ProductVersion': '12.0',
  'RestoreVersion': '21.1.534.5.4,0',
  'ScanRequestCount': 1,
  'SessionId': '...',
  'Supervised': 'false',
  'TrainName': 'StarBravoSeed'
 }
 
 Currently Pallas will not provide updates to any machine with gibraltar ProductType but non-ap (e.g. J137AP) and non-VMM HWModelStr.
 To workaround this issue we hook sysctls used by softwareupdated and com.apple.Mobile to report VMM-x86_64 in HWModelStr.
 The VMM model is chosen if the hypervisor sysctl returns true.
**/

#define CTL_MAXNAME     12      /* largest number of components supported */

#define CTLTYPE             0xf             /* Mask for the type */
#define CTLTYPE_NODE        1               /* name is a node */
#define CTLTYPE_INT         2               /* name describes an integer */
#define CTLTYPE_STRING      3               /* name describes a string */
#define CTLTYPE_QUAD        4               /* name describes a 64-bit number */
#define CTLTYPE_OPAQUE      5               /* name describes a structure */
#define CTLTYPE_STRUCT      CTLTYPE_OPAQUE  /* name describes a structure */

#define SYSCTL_OUT(r, p, l) (r->oldfunc)(r, p, l)

#define OID_MUTABLE_ANCHOR    (INT_MIN)

#define HW_PRODUCT      27

typedef int (* sysctl_handler_t)(struct sysctl_oid *oidp __unused, void *arg1 __unused, int arg2 __unused, struct sysctl_req *req);

struct sysctl_oid {
    struct sysctl_oid_list * oid_parent;
    SLIST_ENTRY(sysctl_oid) oid_link;
    int             oid_number;
    int             oid_kind;
    void            *oid_arg1;
    int             oid_arg2;
    const char      *oid_name;
    int             (*oid_handler)(struct sysctl_oid *oidp __unused, void *arg1 __unused, int arg2 __unused, struct sysctl_req *req);
    const char      *oid_fmt;
    const char      *oid_descr; /* offsetof() field / long description */
    int             oid_version;
    int             oid_refcnt;
};

struct sysctl_oid_iterator {
    struct sysctl_oid *a;
    struct sysctl_oid *b;
};

struct sysctl_req {
    struct proc     *p;
    int             lock;
    user_addr_t     oldptr;         /* pointer to user supplied buffer */
    size_t          oldlen;         /* user buffer length (also returned) */
    size_t          oldidx;         /* total data iteratively copied out */
    int             (*oldfunc)(struct sysctl_req *, const void *, size_t);
    user_addr_t     newptr;         /* buffer containing new value */
    size_t          newlen;         /* length of new value */
    size_t          newidx;         /* total data iteratively copied in */
    int             (*newfunc)(struct sysctl_req *, void *, size_t);
};

SLIST_HEAD(sysctl_oid_list, sysctl_oid);

static struct sysctl_oid_iterator sysctl_oid_iterator_begin(struct sysctl_oid_list *l) {
    struct sysctl_oid_iterator it = { };
    struct sysctl_oid *a = SLIST_FIRST(l);

    if (a == NULL) {
        return it;
    }

    if (a->oid_number == OID_MUTABLE_ANCHOR) {
        it.a = SLIST_NEXT(a, oid_link);
        it.b = SLIST_FIRST((struct sysctl_oid_list *)a->oid_arg1);
    } else {
        it.a = a;
    }
    return it;
}

static struct sysctl_oid *sysctl_oid_iterator_next_system_order(struct sysctl_oid_iterator *it) {
    struct sysctl_oid *a = it->a;
    struct sysctl_oid *b = it->b;

    if (a) {
        it->a = SLIST_NEXT(a, oid_link);
        return a;
    }

    if (b) {
        it->b = SLIST_NEXT(b, oid_link);
        return b;
    }

    return NULL;
}

static sysctl_oid *sysctl_by_name(sysctl_oid_list *sysctl_children, const char *orgname) {
    char named[64];
    strlcpy(named, orgname, sizeof(named));
    char *name = named;
    
    struct sysctl_oid_iterator it;
    struct sysctl_oid *oidp;
    char *p;
    char i;

    p = name + strlen(name) - 1;
    if (*p == '.') {
        *p = '\0';
    }

    size_t len = 0;

    for (p = name; *p && *p != '.'; p++) {
        ;
    }
    i = *p;
    if (i == '.') {
        *p = '\0';
    }

    it = sysctl_oid_iterator_begin(sysctl_children);
    oidp = sysctl_oid_iterator_next_system_order(&it);

    while (oidp && len < CTL_MAXNAME) {
        if (strcmp(name, oidp->oid_name)) {
            oidp = sysctl_oid_iterator_next_system_order(&it);
            continue;
        }
        (len)++;

        if (i == '\0') {
            return oidp;
        }

        if ((oidp->oid_kind & CTLTYPE) != CTLTYPE_NODE) {
            break;
        }

        if (oidp->oid_handler) {
            break;
        }

        it = sysctl_oid_iterator_begin((struct sysctl_oid_list *)oidp->oid_arg1);
        oidp = sysctl_oid_iterator_next_system_order(&it);

        *p = i; /* restore */
        name = p + 1;
        for (p = name; *p && *p != '.'; p++) {
            ;
        }
        i = *p;
        if (i == '.') {
            *p = '\0';
        }
    }
    return nullptr;
}

static mach_vm_address_t org_sysctl_vmm_present;
static int my_sysctl_vmm_present(__unused struct sysctl_oid *oidp, __unused void *arg1, int arg2, struct sysctl_req *req) {
    char procname[64];
    proc_name(proc_pid(req->p), procname, sizeof(procname));
    // SYSLOG("supd", "\n\n\n\nsoftwareupdated vmm_present %d - >>> %s <<<<\n\n\n\n", arg2, procname);
    if (strcmp(procname, "softwareupdated") == 0 || strcmp(procname, "com.apple.Mobile") == 0) {
        int hv_vmm_present = 1;
        return SYSCTL_OUT(req, &hv_vmm_present, sizeof(hv_vmm_present));
    }
    
    return FunctionCast(my_sysctl_vmm_present, org_sysctl_vmm_present)(oidp, arg1, arg2, req);
}

void enableSoftwareUpdates(KernelPatcher &patcher) {
    auto sysctl_children = reinterpret_cast<sysctl_oid_list *>(patcher.solveSymbol(KernelPatcher::KernelID, "_sysctl__children"));
    if (!sysctl_children) {
        SYSLOG("supd", "failed to resolve _sysctl__children");
        return;
    }
	
	// WARN: sysctl_children access should be locked. Unfortunately the lock is not exported.
    sysctl_oid *vmm_present = sysctl_by_name(sysctl_children, "kern.hv_vmm_present");
    if (!vmm_present) {
        SYSLOG("supd", "failed to resolve kern.hv_vmm_present sysctl");
        return;
    }
    
    org_sysctl_vmm_present = patcher.routeFunction(reinterpret_cast<mach_vm_address_t>(vmm_present->oid_handler), reinterpret_cast<mach_vm_address_t>(my_sysctl_vmm_present), true);
    if (!org_sysctl_vmm_present) {
        SYSLOG("supd", "failed to route kern.hv_vmm_present sysctl");
        patcher.clearError();
        return;
    }
}
