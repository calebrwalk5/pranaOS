/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#pragma once

// includes
#include <base/HashMap.h>
#include <base/RefCounted.h>
#include <base/RefPtr.h>
#include <kernel/Forward.h>
#include <kernel/memory/PhysicalPage.h>
#include <kernel/memory/VirtualRangeAllocator.h>

namespace Kernel::Memory {

class PageDirectory : public RefCounted<PageDirectory> {
    friend class MemoryManager;

public:
    static RefPtr<PageDirectory> try_create_for_userspace(VirtualRangeAllocator const* parent_range_allocator = nullptr);
    static NonnullRefPtr<PageDirectory> must_create_kernel_page_directory();
    static RefPtr<PageDirectory> find_by_cr3(FlatPtr);

    ~PageDirectory();

    void allocate_kernel_directory();

    FlatPtr cr3() const
    {
#if ARCH(X86_64)
        return m_pml4t->paddr().get();
#else
        return m_directory_table->paddr().get();
#endif
    }

    VirtualRangeAllocator& range_allocator() { return m_range_allocator; }
    VirtualRangeAllocator const& range_allocator() const { return m_range_allocator; }

    AddressSpace* address_space() { return m_space; }
    const AddressSpace* address_space() const { return m_space; }

    void set_space(Badge<AddressSpace>, AddressSpace& space) { m_space = &space; }

    RecursiveSpinLock& get_lock() { return m_lock; }

private:
    PageDirectory();

    AddressSpace* m_space { nullptr };
    VirtualRangeAllocator m_range_allocator;
#if ARCH(X86_64)
    RefPtr<PhysicalPage> m_pml4t;
#endif
    RefPtr<PhysicalPage> m_directory_table;
#if ARCH(X86_64)
    RefPtr<PhysicalPage> m_directory_pages[512];
#else
    RefPtr<PhysicalPage> m_directory_pages[4];
#endif
    HashMap<FlatPtr, RefPtr<PhysicalPage>> m_page_tables;
    RecursiveSpinLock m_lock;
};

}