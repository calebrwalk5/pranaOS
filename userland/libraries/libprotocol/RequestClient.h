/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#pragma once

// includes
#include <base/HashMap.h>
#include <libipc/ServerConnection.h>
#include <requestserver/RequestClientEndpoint.h>
#include <requestserver/RequestServerEndpoint.h>

namespace Protocol {

class Request;

class RequestClient final
    : public IPC::ServerConnection<RequestClientEndpoint, RequestServerEndpoint>
    , public RequestClientEndpoint {
    C_OBJECT(RequestClient);

public:
    template<typename RequestHashMapTraits = Traits<String>>
    RefPtr<Request> start_request(String const& method, URL const&, HashMap<String, String, RequestHashMapTraits> const& request_headers = {}, ReadonlyBytes request_body = {});

    bool stop_request(Badge<Request>, Request&);
    bool set_certificate(Badge<Request>, Request&, String, String);

private:
    RequestClient();

    virtual void request_progress(i32, Optional<u32> const&, u32) override;
    virtual void request_finished(i32, bool, u32) override;
    virtual void certificate_requested(i32) override;
    virtual void headers_became_available(i32, IPC::Dictionary const&, Optional<u32> const&) override;

    HashMap<i32, RefPtr<Request>> m_requests;
};

}
