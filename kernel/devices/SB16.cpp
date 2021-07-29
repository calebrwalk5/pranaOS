/*
 * Copyright (c) 2021, AakeshDarsh, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */


// includes
#include <base/Memory.h>
#include <base/Singleton.h>
#include <base/StringView.h>
#include <kernel/arch/x86/InterruptDisabler.h>
#include <kernel/Debug.h>
#include <kernel/devices/SB16.h>
#include <kernel/IO.h>
#include <kernel/Sections.h>
#include <kernel/Thread.h>
#include <kernel/vm/AnonymousVMObject.h>
#include <kernel/vm/MemoryManager.h>

namespace Kernel {
#define SB16_DEFAULT_IRQ 5

enum class SampleFormat : u8 {
    Signed = 0x10,
    Stereo = 0x20,
};

const u16 DSP_READ = 0x22A;
const u16 DSP_WRITE = 0x22C;
const u16 DSP_STATUS = 0x22E;
const u16 DSP_R_ACK = 0x22F;

void SB16::dsp_write(u8 value)
{
    while (IO::in8(DSP_WRITE) & 0x80) {
        ;
    }
    IO::out8(DSP_WRITE, value);
}

u8 SB16::dsp_read()
{
    while (!(IO::in8(DSP_STATUS) & 0x80)) {
        ;
    }
    return IO::in8(DSP_READ);
}

void SB16::set_sample_rate(uint16_t hz)
{
    dsp_write(0x41); 
    dsp_write((u8)(hz >> 8));
    dsp_write((u8)hz);
    dsp_write(0x42); 
    dsp_write((u8)(hz >> 8));
    dsp_write((u8)hz);
}

static AK::Singleton<SB16> s_the;

UNMAP_AFTER_INIT SB16::SB16()
    : IRQHandler(SB16_DEFAULT_IRQ)
    , CharacterDevice(42, 42) 
{
    initialize();
}

UNMAP_AFTER_INIT SB16::~SB16()
{
}

UNMAP_AFTER_INIT void SB16::detect()
{
    IO::out8(0x226, 1);
    IO::delay(32);
    IO::out8(0x226, 0);

    auto data = dsp_read();
    if (data != 0xaa) {
        return;
    }
    SB16::create();
}

UNMAP_AFTER_INIT void SB16::create()
{
    s_the.ensure_instance();
}

SB16& SB16::the()
{
    return *s_the;
}

UNMAP_AFTER_INIT void SB16::initialize()
{
    disable_irq();

    IO::out8(0x226, 1);
    IO::delay(32);
    IO::out8(0x226, 0);

    auto data = dsp_read();
    if (data != 0xaa) {
        dbgln("SB16: SoundBlaster not ready");
        return;
    }

    dsp_write(0xe1);
    m_major_version = dsp_read();
    auto vmin = dsp_read();

    dmesgln("SB16: Found version {}.{}", m_major_version, vmin);
    set_irq_register(SB16_DEFAULT_IRQ);
    dmesgln("SB16: IRQ {}", get_irq_line());
}

void SB16::set_irq_register(u8 irq_number)
{
    u8 bitmask;
    switch (irq_number) {
    case 2:
        bitmask = 0;
        break;
    case 5:
        bitmask = 0b10;
        break;
    case 7:
        bitmask = 0b100;
        break;
    case 10:
        bitmask = 0b1000;
        break;
    default:
        VERIFY_NOT_REACHED();
    }
    IO::out8(0x224, 0x80);
    IO::out8(0x225, bitmask);
}

u8 SB16::get_irq_line()
{
    IO::out8(0x224, 0x80);
    u8 bitmask = IO::in8(0x225);
    switch (bitmask) {
    case 0:
        return 2;
    case 0b10:
        return 5;
    case 0b100:
        return 7;
    case 0b1000:
        return 10;
    }
    return bitmask;
}
void SB16::set_irq_line(u8 irq_number)
{
    InterruptDisabler disabler;
    if (irq_number == get_irq_line())
        return;
    set_irq_register(irq_number);
    change_irq_number(irq_number);
}

bool SB16::can_read(const FileDescription&, size_t) const
{
    return false;
}

KResultOr<size_t> SB16::read(FileDescription&, u64, UserOrKernelBuffer&, size_t)
{
    return 0;
}

void SB16::dma_start(uint32_t length)
{
    const auto addr = m_dma_region->physical_page(0)->paddr().get();
    const u8 channel = 5; 
    const u8 mode = 0x48;

    IO::out8(0xd4, 4 + (channel % 4));

    IO::out8(0xd8, 0);

    IO::out8(0xd6, (channel % 4) | mode);

    u16 offset = (addr / 2) % 65536;
    IO::out8(0xc4, (u8)offset);
    IO::out8(0xc4, (u8)(offset >> 8));

    IO::out8(0xc6, (u8)(length - 1));
    IO::out8(0xc6, (u8)((length - 1) >> 8));

    IO::out8(0x8b, addr >> 16);
    auto page_number = addr >> 16;
    VERIFY(page_number <= NumericLimits<u8>::max());
    IO::out8(0x8b, page_number);

    IO::out8(0xd4, (channel % 4));
}

bool SB16::handle_irq(const RegisterState&)
{

    dsp_write(0xd5);

    IO::in8(DSP_STATUS); 
    if (m_major_version >= 4)
        IO::in8(DSP_R_ACK); 

    m_irq_queue.wake_all();
    return true;
}

void SB16::wait_for_irq()
{
    m_irq_queue.wait_forever("SB16");
    disable_irq();
}

KResultOr<size_t> SB16::write(FileDescription&, u64, const UserOrKernelBuffer& data, size_t length)
{
    if (!m_dma_region) {
        auto page = MM.allocate_supervisor_physical_page();
        if (!page)
            return ENOMEM;
        auto nonnull_page = page.release_nonnull();
        auto vmobject = AnonymousVMObject::try_create_with_physical_pages({ &nonnull_page, 1 });
        if (!vmobject)
            return ENOMEM;
        m_dma_region = MM.allocate_kernel_region_with_vmobject(*vmobject, PAGE_SIZE, "SB16 DMA buffer", Region::Access::Write);
        if (!m_dma_region)
            return ENOMEM;
    }

    dbgln_if(SB16_DEBUG, "SB16: Writing buffer of {} bytes", length);

    VERIFY(length <= PAGE_SIZE);
    const int BLOCK_SIZE = 32 * 1024;
    if (length > BLOCK_SIZE) {
        return ENOSPC;
    }

    u8 mode = (u8)SampleFormat::Signed | (u8)SampleFormat::Stereo;

    const int sample_rate = 44100;
    set_sample_rate(sample_rate);
    if (!data.read(m_dma_region->vaddr().as_ptr(), length))
        return EFAULT;
    dma_start(length);

    u8 command = 0xb0;

    u16 sample_count = length / sizeof(i16);
    if (mode & (u8)SampleFormat::Stereo)
        sample_count /= 2;

    sample_count -= 1;

    cli();
    enable_irq();

    dsp_write(command);
    dsp_write(mode);
    dsp_write((u8)sample_count);
    dsp_write((u8)(sample_count >> 8));

    wait_for_irq();
    return length;
}

}