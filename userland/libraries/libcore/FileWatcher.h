/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <base/EnumBits.h>
#include <base/Function.h>
#include <base/Noncopyable.h>
#include <base/NonnullRefPtr.h>
#include <base/RefCounted.h>
#include <base/Result.h>
#include <base/String.h>
#include <kernel/api/InodeWatcherEvent.h>
#include <kernel/api/InodeWatcherFlags.h>
#include <libcore/Notifier.h>

namespace Core {

struct FileWatcherEvent {
    enum class Type {
        Invalid = 0,
        MetadataModified = 1 << 0,
        ContentModified = 1 << 1,
        Deleted = 1 << 2,
        ChildCreated = 1 << 3,
        ChildDeleted = 1 << 4,
    };
    Type type;
    String event_path;
};

AK_ENUM_BITWISE_OPERATORS(FileWatcherEvent::Type);

class FileWatcherBase {
public:
    virtual ~FileWatcherBase() { }

    Result<bool, String> add_watch(String path, FileWatcherEvent::Type event_mask);
    Result<bool, String> remove_watch(String path);
    bool is_watching(String const& path) const { return m_path_to_wd.find(path) != m_path_to_wd.end(); }

protected:
    FileWatcherBase(int watcher_fd)
        : m_watcher_fd(watcher_fd)
    {
    }

    int m_watcher_fd { -1 };
    HashMap<String, unsigned> m_path_to_wd;
    HashMap<unsigned, String> m_wd_to_path;
};

class BlockingFileWatcher final : public FileWatcherBase {
    AK_MAKE_NONCOPYABLE(BlockingFileWatcher);

public:
    explicit BlockingFileWatcher(InodeWatcherFlags = InodeWatcherFlags::None);
    ~BlockingFileWatcher();

    Optional<FileWatcherEvent> wait_for_event();
};

class FileWatcher final : public FileWatcherBase
    , public RefCounted<FileWatcher> {
    AK_MAKE_NONCOPYABLE(FileWatcher);

public:
    static Result<NonnullRefPtr<FileWatcher>, String> create(InodeWatcherFlags = InodeWatcherFlags::None);
    ~FileWatcher();

    Function<void(FileWatcherEvent const&)> on_change;

private:
    FileWatcher(int watcher_fd, NonnullRefPtr<Notifier>);

    NonnullRefPtr<Notifier> m_notifier;
};

}

namespace AK {

template<>
struct Formatter<Core::FileWatcherEvent> : Formatter<FormatString> {
    void format(FormatBuilder& builder, const Core::FileWatcherEvent& value)
    {
        Formatter<FormatString>::format(builder, "FileWatcherEvent(\"{}\", {})", value.event_path, value.type);
    }
};

template<>
struct Formatter<Core::FileWatcherEvent::Type> : Formatter<FormatString> {
    void format(FormatBuilder& builder, const Core::FileWatcherEvent::Type& value)
    {
        char const* type;
        switch (value) {
        case Core::FileWatcherEvent::Type::ChildCreated:
            type = "ChildCreated";
            break;
        case Core::FileWatcherEvent::Type::ChildDeleted:
            type = "ChildDeleted";
            break;
        case Core::FileWatcherEvent::Type::Deleted:
            type = "Deleted";
            break;
        case Core::FileWatcherEvent::Type::ContentModified:
            type = "ContentModified";
            break;
        case Core::FileWatcherEvent::Type::MetadataModified:
            type = "MetadataModified";
            break;
        default:
            VERIFY_NOT_REACHED();
        }

        builder.put_string(type);
    }
};

}