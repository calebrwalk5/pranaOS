/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// includes
#include <base/Badge.h>
#include <base/Debug.h>
#include <base/Format.h>
#include <base/IDAllocator.h>
#include <base/JsonObject.h>
#include <base/JsonValue.h>
#include <base/NeverDestroyed.h>
#include <base/Singleton.h>
#include <base/TemporaryChange.h>
#include <base/Time.h>
#include <libcore/Event.h>
#include <libcore/EventLoop.h>
#include <libcore/LocalServer.h>
#include <libcore/LocalSocket.h>
#include <libcore/Notifier.h>
#include <libcore/Object.h>
#include <libthreading/Mutex.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

namespace Core {

class InspectorServerConnection;

[[maybe_unused]] static bool connect_to_inspector_server();

struct EventLoopTimer {
    int timer_id { 0 };
    int interval { 0 };
    timeval fire_time { 0, 0 };
    bool should_reload { false };
    TimerShouldFireWhenNotVisible fire_when_not_visible { TimerShouldFireWhenNotVisible::No };
    WeakPtr<Object> owner;

    void reload(const timeval& now);
    bool has_expired(const timeval& now) const;
};

struct EventLoop::Private {
    Threading::Mutex lock;
};

static EventLoop* s_main_event_loop;
static Vector<EventLoop&>* s_event_loop_stack;
static NeverDestroyed<IDAllocator> s_id_allocator;
static HashMap<int, NonnullOwnPtr<EventLoopTimer>>* s_timers;
static HashTable<Notifier*>* s_notifiers;
int EventLoop::s_wake_pipe_fds[2];
static RefPtr<InspectorServerConnection> s_inspector_server_connection;

class SignalHandlers : public RefCounted<SignalHandlers> {
    BASE_MAKE_NONCOPYABLE(SignalHandlers);
    BASE_MAKE_NONMOVABLE(SignalHandlers);

public:
    SignalHandlers(int signo, void (*handle_signal)(int));
    ~SignalHandlers();

    void dispatch();
    int add(Function<void(int)>&& handler);
    bool remove(int handler_id);

    bool is_empty() const
    {
        if (m_calling_handlers) {
            for (auto& handler : m_handlers_pending) {
                if (handler.value)
                    return false; 
            }
        }
        return m_handlers.is_empty();
    }

    bool have(int handler_id) const
    {
        if (m_calling_handlers) {
            auto it = m_handlers_pending.find(handler_id);
            if (it != m_handlers_pending.end()) {
                if (!it->value)
                    return false; 
            }
        }
        return m_handlers.contains(handler_id);
    }

    int m_signo;
    void (*m_original_handler)(int); 
    HashMap<int, Function<void(int)>> m_handlers;
    HashMap<int, Function<void(int)>> m_handlers_pending;
    bool m_calling_handlers { false };
};

struct SignalHandlersInfo {
    HashMap<int, NonnullRefPtr<SignalHandlers>> signal_handlers;
    int next_signal_id { 0 };
};

static Singleton<SignalHandlersInfo> s_signals;
template<bool create_if_null = true>
inline SignalHandlersInfo* signals_info()
{
    return s_signals.ptr();
}

pid_t EventLoop::s_pid;

class InspectorServerConnection : public Object {
    C_OBJECT(InspectorServerConnection)
public:
    explicit InspectorServerConnection(RefPtr<LocalSocket> socket)
        : m_socket(move(socket))
        , m_client_id(s_id_allocator->allocate())
    {
#ifdef __serenity__
        add_child(*m_socket);
        m_socket->on_ready_to_read = [this] {
            u32 length;
            int nread = m_socket->read((u8*)&length, sizeof(length));
            if (nread == 0) {
                dbgln_if(EVENTLOOP_DEBUG, "RPC client disconnected");
                shutdown();
                return;
            }
            VERIFY(nread == sizeof(length));
            auto request = m_socket->read(length);

            auto request_json = JsonValue::from_string(request);
            if (!request_json.has_value() || !request_json.value().is_object()) {
                dbgln("RPC client sent invalid request");
                shutdown();
                return;
            }

            handle_request(request_json.value().as_object());
        };
#else
        warnln("RPC Client constructed outside serenity, this is very likely a bug!");
#endif
    }
    virtual ~InspectorServerConnection() override
    {
        if (auto inspected_object = m_inspected_object.strong_ref())
            inspected_object->decrement_inspector_count({});
    }

    void send_response(const JsonObject& response)
    {
        auto serialized = response.to_string();
        u32 length = serialized.length();
        m_socket->write((const u8*)&length, sizeof(length));
        m_socket->write(serialized);
    }

    void handle_request(const JsonObject& request)
    {
        auto type = request.get("type").as_string_or({});

        if (type.is_null()) {
            dbgln("RPC client sent request without type field");
            return;
        }

        if (type == "Identify") {
            JsonObject response;
            response.set("type", type);
            response.set("pid", getpid());
#ifdef __serenity__
            char buffer[1024];
            if (get_process_name(buffer, sizeof(buffer)) >= 0) {
                response.set("process_name", buffer);
            } else {
                response.set("process_name", JsonValue());
            }
#endif
            send_response(response);
            return;
        }

        if (type == "GetAllObjects") {
            JsonObject response;
            response.set("type", type);
            JsonArray objects;
            for (auto& object : Object::all_objects()) {
                JsonObject json_object;
                object.save_to(json_object);
                objects.append(move(json_object));
            }
            response.set("objects", move(objects));
            send_response(response);
            return;
        }

        if (type == "SetInspectedObject") {
            auto address = request.get("address").to_number<FlatPtr>();
            for (auto& object : Object::all_objects()) {
                if ((FlatPtr)&object == address) {
                    if (auto inspected_object = m_inspected_object.strong_ref())
                        inspected_object->decrement_inspector_count({});
                    m_inspected_object = object;
                    object.increment_inspector_count({});
                    break;
                }
            }
            return;
        }

        if (type == "SetProperty") {
            auto address = request.get("address").to_number<FlatPtr>();
            for (auto& object : Object::all_objects()) {
                if ((FlatPtr)&object == address) {
                    bool success = object.set_property(request.get("name").to_string(), request.get("value"));
                    JsonObject response;
                    response.set("type", "SetProperty");
                    response.set("success", success);
                    send_response(response);
                    break;
                }
            }
            return;
        }

        if (type == "Disconnect") {
            shutdown();
            return;
        }
    }

    void shutdown()
    {
        s_id_allocator->deallocate(m_client_id);
    }

private:
    RefPtr<LocalSocket> m_socket;
    WeakPtr<Object> m_inspected_object;
    int m_client_id { -1 };
};

EventLoop::EventLoop([[maybe_unused]] MakeInspectable make_inspectable)
    : m_private(make<Private>())
{
    if (!s_event_loop_stack) {
        s_event_loop_stack = new Vector<EventLoop&>;
        s_timers = new HashMap<int, NonnullOwnPtr<EventLoopTimer>>;
        s_notifiers = new HashTable<Notifier*>;
    }

    if (!s_main_event_loop) {
        s_main_event_loop = this;
        s_pid = getpid();
#if defined(SOCK_NONBLOCK)
        int rc = pipe2(s_wake_pipe_fds, O_CLOEXEC);
#else
        int rc = pipe(s_wake_pipe_fds);
        fcntl(s_wake_pipe_fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(s_wake_pipe_fds[1], F_SETFD, FD_CLOEXEC);

#endif
        VERIFY(rc == 0);
        s_event_loop_stack->append(*this);

#ifdef __serenity__
        if (getuid() != 0
            && make_inspectable == MakeInspectable::Yes
            && !s_inspector_server_connection) {
            if (!connect_to_inspector_server())
                dbgln("Core::EventLoop: Failed to connect to InspectorServer");
        }
#endif
    }

    dbgln_if(EVENTLOOP_DEBUG, "{} Core::EventLoop constructed :)", getpid());
}

EventLoop::~EventLoop()
{
}

bool connect_to_inspector_server()
{
#ifdef __serenity__
    auto socket = Core::LocalSocket::construct();
    if (!socket->connect(SocketAddress::local("/tmp/portal/inspectables")))
        return false;
    s_inspector_server_connection = InspectorServerConnection::construct(move(socket));
    return true;
#else
    VERIFY_NOT_REACHED();
#endif
}

EventLoop& EventLoop::main()
{
    VERIFY(s_main_event_loop);
    return *s_main_event_loop;
}

EventLoop& EventLoop::current()
{
    return s_event_loop_stack->last();
}

void EventLoop::quit(int code)
{
    dbgln_if(EVENTLOOP_DEBUG, "Core::EventLoop::quit({})", code);
    m_exit_requested = true;
    m_exit_code = code;
}

void EventLoop::unquit()
{
    dbgln_if(EVENTLOOP_DEBUG, "Core::EventLoop::unquit()");
    m_exit_requested = false;
    m_exit_code = 0;
}

struct EventLoopPusher {
public:
    EventLoopPusher(EventLoop& event_loop)
        : m_event_loop(event_loop)
    {
        if (&m_event_loop != s_main_event_loop) {
            m_event_loop.take_pending_events_from(EventLoop::current());
            s_event_loop_stack->append(event_loop);
        }
    }
    ~EventLoopPusher()
    {
        if (&m_event_loop != s_main_event_loop) {
            s_event_loop_stack->take_last();
            EventLoop::current().take_pending_events_from(m_event_loop);
        }
    }

private:
    EventLoop& m_event_loop;
};

int EventLoop::exec()
{
    EventLoopPusher pusher(*this);
    for (;;) {
        if (m_exit_requested)
            return m_exit_code;
        pump();
    }
    VERIFY_NOT_REACHED();
}

void EventLoop::pump(WaitMode mode)
{
    wait_for_event(mode);

    decltype(m_queued_events) events;
    {
        Threading::MutexLocker locker(m_private->lock);
        events = move(m_queued_events);
    }

    for (size_t i = 0; i < events.size(); ++i) {
        auto& queued_event = events.at(i);
        auto receiver = queued_event.receiver.strong_ref();
        auto& event = *queued_event.event;
        if (receiver)
            dbgln_if(EVENTLOOP_DEBUG, "Core::EventLoop: {} event {}", *receiver, event.type());

        if (!receiver) {
            switch (event.type()) {
            case Event::Quit:
                VERIFY_NOT_REACHED();
                return;
            default:
                dbgln_if(EVENTLOOP_DEBUG, "Event type {} with no receiver :(", event.type());
                break;
            }
        } else if (event.type() == Event::Type::DeferredInvoke) {
            dbgln_if(DEFERRED_INVOKE_DEBUG, "DeferredInvoke: receiver = {}", *receiver);
            static_cast<DeferredInvocationEvent&>(event).m_invokee(*receiver);
        } else {
            NonnullRefPtr<Object> protector(*receiver);
            receiver->dispatch_event(event);
        }

        if (m_exit_requested) {
            Threading::MutexLocker locker(m_private->lock);
            dbgln_if(EVENTLOOP_DEBUG, "Core::EventLoop: Exit requested. Rejigging {} events.", events.size() - i);
            decltype(m_queued_events) new_event_queue;
            new_event_queue.ensure_capacity(m_queued_events.size() + events.size());
            for (++i; i < events.size(); ++i)
                new_event_queue.unchecked_append(move(events[i]));
            new_event_queue.extend(move(m_queued_events));
            m_queued_events = move(new_event_queue);
            return;
        }
    }
}

void EventLoop::post_event(Object& receiver, NonnullOwnPtr<Event>&& event)
{
    Threading::MutexLocker lock(m_private->lock);
    dbgln_if(EVENTLOOP_DEBUG, "Core::EventLoop::post_event: ({}) << receivier={}, event={}", m_queued_events.size(), receiver, event);
    m_queued_events.empend(receiver, move(event));
}

SignalHandlers::SignalHandlers(int signo, void (*handle_signal)(int))
    : m_signo(signo)
    , m_original_handler(signal(signo, handle_signal))
{
    dbgln_if(EVENTLOOP_DEBUG, "Core::EventLoop: Registered handler for signal {}", m_signo);
}

SignalHandlers::~SignalHandlers()
{
    dbgln_if(EVENTLOOP_DEBUG, "Core::EventLoop: Unregistering handler for signal {}", m_signo);
    signal(m_signo, m_original_handler);
}

void SignalHandlers::dispatch()
{
    TemporaryChange change(m_calling_handlers, true);
    for (auto& handler : m_handlers)
        handler.value(m_signo);
    if (!m_handlers_pending.is_empty()) {

        for (auto& handler : m_handlers_pending) {
            if (handler.value) {
                auto result = m_handlers.set(handler.key, move(handler.value));
                VERIFY(result == AK::HashSetResult::InsertedNewEntry);
            } else {
                m_handlers.remove(handler.key);
            }
        }
        m_handlers_pending.clear();
    }
}

int SignalHandlers::add(Function<void(int)>&& handler)
{
    int id = ++signals_info()->next_signal_id; 
    if (m_calling_handlers)
        m_handlers_pending.set(id, move(handler));
    else
        m_handlers.set(id, move(handler));
    return id;
}

bool SignalHandlers::remove(int handler_id)
{
    VERIFY(handler_id != 0);
    if (m_calling_handlers) {
        auto it = m_handlers.find(handler_id);
        if (it != m_handlers.end()) {

            m_handlers_pending.set(handler_id, {});
            return true;
        }
        it = m_handlers_pending.find(handler_id);
        if (it != m_handlers_pending.end()) {
            if (!it->value)
                return false; 
            it->value = nullptr;
            return true;
        }
        return false;
    }
    return m_handlers.remove(handler_id);
}

void EventLoop::dispatch_signal(int signo)
{
    auto& info = *signals_info();
    auto handlers = info.signal_handlers.find(signo);
    if (handlers != info.signal_handlers.end()) {

        auto handler = handlers->value;
        dbgln_if(EVENTLOOP_DEBUG, "Core::EventLoop: dispatching signal {}", signo);
        handler->dispatch();
    }
}

void EventLoop::handle_signal(int signo)
{
    VERIFY(signo != 0);

    if (getpid() == s_pid) {
        int nwritten = write(s_wake_pipe_fds[1], &signo, sizeof(signo));
        if (nwritten < 0) {
            perror("EventLoop::register_signal: write");
            VERIFY_NOT_REACHED();
        }
    } else {

        s_pid = 0;
    }
}

int EventLoop::register_signal(int signo, Function<void(int)> handler)
{
    VERIFY(signo != 0);
    auto& info = *signals_info();
    auto handlers = info.signal_handlers.find(signo);
    if (handlers == info.signal_handlers.end()) {
        auto signal_handlers = adopt_ref(*new SignalHandlers(signo, EventLoop::handle_signal));
        auto handler_id = signal_handlers->add(move(handler));
        info.signal_handlers.set(signo, move(signal_handlers));
        return handler_id;
    } else {
        return handlers->value->add(move(handler));
    }
}

void EventLoop::unregister_signal(int handler_id)
{
    VERIFY(handler_id != 0);
    int remove_signo = 0;
    auto& info = *signals_info();
    for (auto& h : info.signal_handlers) {
        auto& handlers = *h.value;
        if (handlers.remove(handler_id)) {
            if (handlers.is_empty())
                remove_signo = handlers.m_signo;
            break;
        }
    }
    if (remove_signo != 0)
        info.signal_handlers.remove(remove_signo);
}

void EventLoop::notify_forked(ForkEvent event)
{
    switch (event) {
    case ForkEvent::Child:
        s_main_event_loop = nullptr;
        s_event_loop_stack->clear();
        s_timers->clear();
        s_notifiers->clear();
        if (auto* info = signals_info<false>()) {
            info->signal_handlers.clear();
            info->next_signal_id = 0;
        }
        s_pid = 0;
#ifdef __serenity__
        s_inspector_server_connection = nullptr;
#endif
        return;
    }

    VERIFY_NOT_REACHED();
}

void EventLoop::wait_for_event(WaitMode mode)
{
    fd_set rfds;
    fd_set wfds;
retry:
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    int max_fd = 0;
    auto add_fd_to_set = [&max_fd](int fd, fd_set& set) {
        FD_SET(fd, &set);
        if (fd > max_fd)
            max_fd = fd;
    };

    int max_fd_added = -1;
    add_fd_to_set(s_wake_pipe_fds[0], rfds);
    max_fd = max(max_fd, max_fd_added);
    for (auto& notifier : *s_notifiers) {
        if (notifier->event_mask() & Notifier::Read)
            add_fd_to_set(notifier->fd(), rfds);
        if (notifier->event_mask() & Notifier::Write)
            add_fd_to_set(notifier->fd(), wfds);
        if (notifier->event_mask() & Notifier::Exceptional)
            VERIFY_NOT_REACHED();
    }

    bool queued_events_is_empty;
    {
        Threading::MutexLocker locker(m_private->lock);
        queued_events_is_empty = m_queued_events.is_empty();
    }

    timeval now;
    struct timeval timeout = { 0, 0 };
    bool should_wait_forever = false;
    if (mode == WaitMode::WaitForEvents && queued_events_is_empty) {
        auto next_timer_expiration = get_next_timer_expiration();
        if (next_timer_expiration.has_value()) {
            timespec now_spec;
            clock_gettime(CLOCK_MONOTONIC_COARSE, &now_spec);
            now.tv_sec = now_spec.tv_sec;
            now.tv_usec = now_spec.tv_nsec / 1000;
            timeval_sub(next_timer_expiration.value(), now, timeout);
            if (timeout.tv_sec < 0 || (timeout.tv_sec == 0 && timeout.tv_usec < 0)) {
                timeout.tv_sec = 0;
                timeout.tv_usec = 0;
            }
        } else {
            should_wait_forever = true;
        }
    }

try_select_again:
    int marked_fd_count = select(max_fd + 1, &rfds, &wfds, nullptr, should_wait_forever ? nullptr : &timeout);
    if (marked_fd_count < 0) {
        int saved_errno = errno;
        if (saved_errno == EINTR) {
            if (m_exit_requested)
                return;
            goto try_select_again;
        }
        dbgln_if(EVENTLOOP_DEBUG, "Core::EventLoop::wait_for_event: {} ({}: {})", marked_fd_count, saved_errno, strerror(saved_errno));
        VERIFY_NOT_REACHED();
    }
    if (FD_ISSET(s_wake_pipe_fds[0], &rfds)) {
        int wake_events[8];
        auto nread = read(s_wake_pipe_fds[0], wake_events, sizeof(wake_events));
        if (nread < 0) {
            perror("read from wake pipe");
            VERIFY_NOT_REACHED();
        }
        VERIFY(nread > 0);
        bool wake_requested = false;
        int event_count = nread / sizeof(wake_events[0]);
        for (int i = 0; i < event_count; i++) {
            if (wake_events[i] != 0)
                dispatch_signal(wake_events[i]);
            else
                wake_requested = true;
        }

        if (!wake_requested && nread == sizeof(wake_events))
            goto retry;
    }

    if (!s_timers->is_empty()) {
        timespec now_spec;
        clock_gettime(CLOCK_MONOTONIC_COARSE, &now_spec);
        now.tv_sec = now_spec.tv_sec;
        now.tv_usec = now_spec.tv_nsec / 1000;
    }

    for (auto& it : *s_timers) {
        auto& timer = *it.value;
        if (!timer.has_expired(now))
            continue;
        auto owner = timer.owner.strong_ref();
        if (timer.fire_when_not_visible == TimerShouldFireWhenNotVisible::No
            && owner && !owner->is_visible_for_timer_purposes()) {
            continue;
        }

        dbgln_if(EVENTLOOP_DEBUG, "Core::EventLoop: Timer {} has expired, sending Core::TimerEvent to {}", timer.timer_id, *owner);

        if (owner)
            post_event(*owner, make<TimerEvent>(timer.timer_id));
        if (timer.should_reload) {
            timer.reload(now);
        } else {

            VERIFY_NOT_REACHED();
        }
    }

    if (!marked_fd_count)
        return;

    for (auto& notifier : *s_notifiers) {
        if (FD_ISSET(notifier->fd(), &rfds)) {
            if (notifier->event_mask() & Notifier::Event::Read)
                post_event(*notifier, make<NotifierReadEvent>(notifier->fd()));
        }
        if (FD_ISSET(notifier->fd(), &wfds)) {
            if (notifier->event_mask() & Notifier::Event::Write)
                post_event(*notifier, make<NotifierWriteEvent>(notifier->fd()));
        }
    }
}

bool EventLoopTimer::has_expired(const timeval& now) const
{
    return now.tv_sec > fire_time.tv_sec || (now.tv_sec == fire_time.tv_sec && now.tv_usec >= fire_time.tv_usec);
}

void EventLoopTimer::reload(const timeval& now)
{
    fire_time = now;
    fire_time.tv_sec += interval / 1000;
    fire_time.tv_usec += (interval % 1000) * 1000;
}

Optional<struct timeval> EventLoop::get_next_timer_expiration()
{
    Optional<struct timeval> soonest {};
    for (auto& it : *s_timers) {
        auto& fire_time = it.value->fire_time;
        auto owner = it.value->owner.strong_ref();
        if (it.value->fire_when_not_visible == TimerShouldFireWhenNotVisible::No
            && owner && !owner->is_visible_for_timer_purposes()) {
            continue;
        }
        if (!soonest.has_value() || fire_time.tv_sec < soonest.value().tv_sec || (fire_time.tv_sec == soonest.value().tv_sec && fire_time.tv_usec < soonest.value().tv_usec))
            soonest = fire_time;
    }
    return soonest;
}

int EventLoop::register_timer(Object& object, int milliseconds, bool should_reload, TimerShouldFireWhenNotVisible fire_when_not_visible)
{
    VERIFY(milliseconds >= 0);
    auto timer = make<EventLoopTimer>();
    timer->owner = object;
    timer->interval = milliseconds;
    timeval now;
    timespec now_spec;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &now_spec);
    now.tv_sec = now_spec.tv_sec;
    now.tv_usec = now_spec.tv_nsec / 1000;
    timer->reload(now);
    timer->should_reload = should_reload;
    timer->fire_when_not_visible = fire_when_not_visible;
    int timer_id = s_id_allocator->allocate();
    timer->timer_id = timer_id;
    s_timers->set(timer_id, move(timer));
    return timer_id;
}

bool EventLoop::unregister_timer(int timer_id)
{
    s_id_allocator->deallocate(timer_id);
    auto it = s_timers->find(timer_id);
    if (it == s_timers->end())
        return false;
    s_timers->remove(it);
    return true;
}

void EventLoop::register_notifier(Badge<Notifier>, Notifier& notifier)
{
    s_notifiers->set(&notifier);
}

void EventLoop::unregister_notifier(Badge<Notifier>, Notifier& notifier)
{
    s_notifiers->remove(&notifier);
}

void EventLoop::wake()
{
    int wake_event = 0;
    int nwritten = write(s_wake_pipe_fds[1], &wake_event, sizeof(wake_event));
    if (nwritten < 0) {
        perror("EventLoop::wake: write");
        VERIFY_NOT_REACHED();
    }
}

EventLoop::QueuedEvent::QueuedEvent(Object& receiver, NonnullOwnPtr<Event> event)
    : receiver(receiver)
    , event(move(event))
{
}

EventLoop::QueuedEvent::QueuedEvent(QueuedEvent&& other)
    : receiver(other.receiver)
    , event(move(other.event))
{
}

EventLoop::QueuedEvent::~QueuedEvent()
{
}

}