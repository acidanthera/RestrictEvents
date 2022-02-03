//
//  vnode_types.hpp
//  RestrictEvents
//
//  Copyright © 2022 vit9696. All rights reserved.
//

#ifndef vnode_types_h
#define vnode_types_h

//
// Private definitions from memory_object_types.h and vnode_pager.h
//
typedef struct memory_object {
	unsigned int	_pad1; /* struct ipc_object_header */
#ifdef __LP64__
	unsigned int	_pad2; /* pad to natural boundary */
#endif
	const void		*mo_pager_ops;
} *memory_object_kernel_t;

typedef natural_t ipc_object_bits_t;

struct ipc_object_header {
	ipc_object_bits_t io_bits;
#ifdef __LP64__
	natural_t         io_padding; /* pad to natural boundary */
#endif
};

typedef struct vnode_pager {
	struct ipc_object_header	pager_header;	/* fake ip_kotype()		*/
	void *pager_ops;	/* == &vnode_pager_ops	     */
	unsigned int		ref_count;	/* reference count	     */
	memory_object_control_t control_handle;	/* mem object control handle */
	struct vnode		*vnode_handle;	/* vnode handle 	     */
} *vnode_pager_t;

#endif /* vnode_types_h */
