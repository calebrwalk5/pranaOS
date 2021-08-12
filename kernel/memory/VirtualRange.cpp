/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/


// includes
#include <base/Vector.h>
#include <kernel/memory/MemoryManager.h>
#include <kernel/kemory/VirtualRange.h>
#include <libc/limits.h>

namespace Kernel::Memory {

Vector<VirtualRange, 2> VirtualRange::carve(VirtualRange const& taken) const
{
    VERIFY((taken.size() % PAGE_SIZE) == 0);

    Vector<VirtualRange, 2> parts;
    if (taken == *this)
        return {};
    if (taken.base() > base())
        parts.append({ base(), taken.base().get() - base().get() });
    if (taken.end() < end())
        parts.append({ taken.end(), end().get() - taken.end().get() });
    return parts;
}
VirtualRange VirtualRange::intersect(VirtualRange const& other) const
{
    if (*this == other) {
        return *this;
    }
    auto new_base = max(base(), other.base());
    auto new_end = min(end(), other.end());
    VERIFY(new_base < new_end);
    return VirtualRange(new_base, (new_end - new_base).get());
}

KResultOr<VirtualRange> VirtualRange::expand_to_page_boundaries(FlatPtr address, size_t size)
{
    if (page_round_up_would_wrap(size))
        return EINVAL;

    if ((address + size) < address)
        return EINVAL;

    if (page_round_up_would_wrap(address + size))
        return EINVAL;

    auto base = VirtualAddress { address }.page_base();
    auto end = page_round_up(address + size);

    return VirtualRange { base, end - base.get() };
}

}
