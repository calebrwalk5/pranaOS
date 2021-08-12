/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <kernel/filesystem/Inode.h>
#include <kernel/memory/PrivateInodeVMObject.h>

namespace Kernel::Memory {

RefPtr<PrivateInodeVMObject> PrivateInodeVMObject::try_create_with_inode(Inode& inode)
{
    return adopt_ref_if_nonnull(new (nothrow) PrivateInodeVMObject(inode, inode.size()));
}

RefPtr<VMObject> PrivateInodeVMObject::try_clone()
{
    return adopt_ref_if_nonnull(new (nothrow) PrivateInodeVMObject(*this));
}

PrivateInodeVMObject::PrivateInodeVMObject(Inode& inode, size_t size)
    : InodeVMObject(inode, size)
{
}

PrivateInodeVMObject::PrivateInodeVMObject(PrivateInodeVMObject const& other)
    : InodeVMObject(other)
{
}

PrivateInodeVMObject::~PrivateInodeVMObject()
{
}

}
