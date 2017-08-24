// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#pragma once

#include "rpcsendadapter.h"
#include <vespa/messagebus/idiscardhandler.h>
#include <vespa/messagebus/ireplyhandler.h>
#include <vespa/messagebus/common.h>
#include <vespa/fnet/frt/invokable.h>
#include <vespa/fnet/frt/invoker.h>

class FRT_ReflectionBuilder;

namespace mbus {

class Error;

class PayLoadFiller
{
public:
    virtual ~PayLoadFiller() { }
    virtual void fill(FRT_Values & v) const = 0;
};

class RPCSend : public RPCSendAdapter,
                public FRT_Invokable,
                public FRT_IRequestWait,
                public IDiscardHandler,
                public IReplyHandler {
protected:
    RPCNetwork *_net;
    string _clientIdent;
    string _serverIdent;

    virtual void build(FRT_ReflectionBuilder & builder) = 0;
    virtual void send(RoutingNode &recipient, const vespalib::Version &version,
                      const PayLoadFiller & filler, uint64_t timeRemaining) = 0;
    /**
     * Send an error reply for a given request.
     *
     * @param request    The FRT request to reply to.
     * @param version    The version to serialize for.
     * @param traceLevel The trace level to set in the reply.
     * @param err        The error to reply with.
     */
    void replyError(FRT_RPCRequest *req, const vespalib::Version &version, uint32_t traceLevel, const Error &err);
public:

    /**
     * Constructs a new instance of this adapter. This object is unusable until
     * its attach() method has been called.
     */
    RPCSend();
    ~RPCSend();

    void attach(RPCNetwork &net) final override;
    void handleDiscard(Context ctx) final override;
    void sendByHandover(RoutingNode &recipient, const vespalib::Version &version,
                        Blob payload, uint64_t timeRemaining) final override;
    void send(RoutingNode &recipient, const vespalib::Version &version,
              BlobRef payload, uint64_t timeRemaining) final override;
};

} // namespace mbus
