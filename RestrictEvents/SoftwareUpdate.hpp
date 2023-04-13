//
//  SoftwareUpdate.hpp
//  RestrictEvents
//
//  Copyright © 2022 vit9696. All rights reserved.
//

#ifndef SoftwareUpdate_h
#define SoftwareUpdate_h

#include <stdint.h>
#include <sys/queue.h>

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

#define kHasF16C        0x04000000

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


extern bool revassetIsSet;
extern bool revsbvmmIsSet;

#endif /* SoftwareUpdate_h */
