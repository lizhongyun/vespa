// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/fastos/fastos.h>
#include <vespa/storage/common/storagelink.h>

#include <vespa/log/log.h>
#include <sstream>
#include <vespa/storageapi/messageapi/storagecommand.h>
#include <vespa/storage/common/bucketmessages.h>
#include <vespa/vespalib/util/backtrace.h>

LOG_SETUP(".application.link");

using std::shared_ptr;
using std::ostringstream;
using namespace storage;
using namespace storage::api;

StorageLink::~StorageLink()
{
}

void StorageLink::push_back(StorageLink::UP link)
{
    if (_state != CREATED) {
        LOG(error, "Attempted to alter chain by adding link %s after link %s "
                   "while state is %s",
            link->toString().c_str(),
            toString().c_str(),
            stateToString(_state));
        assert(false);
    }
    assert(link.get());
    if (isBottom()) {
        link->_up = this;
        _down = std::move(link);
    } else {
        _down->push_back(std::move(link));
    }
}

void StorageLink::open()
{
        // First tag all states as opened, as components are allowed to send
        // messages both ways in onOpen call, in case any component send message
        // up, the link receiving them should have their state as opened.
    StorageLink* link = this;
    while (true) {
        if (link->_state != CREATED) {
            LOG(error, "During open(), link %s should be in CREATED state, "
                       "not in state %s.",
                toString().c_str(),
                stateToString(link->_state));
            assert(false);
        }
        link->_state = OPENED;
        if (link->_down.get() == 0) break;
        link = link->_down.get();
    }
        // When give all links an onOpen call, bottoms up. Do it bottoms up, as
        // links are more likely to send messages down in their onOpen() call
        // than up. Thus, chances are best that the component is ready to
        // receive messages sent during onOpen().
    while (link != 0) {
        link->onOpen();
        link = link->_up;
    }
}

void StorageLink::doneInit()
{
    StorageLink* link = this;
    while (true) {
        link->onDoneInit();
        if (link->_down.get() == 0) break;
        link = link->_down.get();
    }
}

void StorageLink::close()
{
    _state = CLOSING;
    onClose();
    if (!isBottom()) {
        _down->close();
    }
}

void StorageLink::closeNextLink() {
    _down.reset(0);
}

void StorageLink::flush()
{
    if (_state != CLOSING) {
        LOG(error, "During flush(), link %s should be in CLOSING state, "
                   "not in state %s.",
            toString().c_str(),
            stateToString(_state));
        assert(false);
    }
        // First flush down to get all requests out of the system.
    _state = FLUSHINGDOWN;
    LOG(debug, "Flushing link %s on the way down.", toString().c_str());
    onFlush(true);
    LOG(debug, "Flushed link %s on the way down.", toString().c_str());
    if (!isBottom()) {
        _down->flush();
            // Then flush up to get replies out of the system
        LOG(debug, "Flushing link %s on the way back up.", toString().c_str());
        _state = FLUSHINGUP;
        onFlush(false);
        LOG(debug, "Flushed link %s on the way back up.", toString().c_str());
    } else {
            // Then flush up to get replies out of the system
        LOG(debug, "Flushing link %s on the way back up.", toString().c_str());
        _state = FLUSHINGUP;
        onFlush(false);
        LOG(debug, "Flushed link %s on the way back up.", toString().c_str());
    }
    _state = CLOSED;
    LOG(debug, "Link %s is now closed and should do nothing more.",
        toString().c_str());
}

void StorageLink::sendDown(const StorageMessage::SP& msg)
{
        // Verify acceptable state to send messages down
    switch(_state) {
        case OPENED:
        case CLOSING:
        case FLUSHINGDOWN:
            break;
        default:
            LOG(error,
                "Link %s trying to send %s down while in state %s",
                toString().c_str(),
                msg->toString().c_str(),
                stateToString(_state));
            assert(false);
    }
    assert(msg.get());
    LOG(spam, "Storage Link %s to handle %s",
        toString().c_str(), msg->toString().c_str());
    if (isBottom()) {
        LOG(spam, "Storage link %s at bottom of chain got message %s.",
            toString().c_str(), msg->toString().c_str());
        /*
        if (isFlush(msg)) {
            StorageCommand& cmd = static_cast<StorageCommand&>(*msg);
            shared_ptr<StorageReply> reply(cmd.makeReply().release());

            if (reply.get()) {
                sendUp(reply);
            }
        } else {
        */
            ostringstream ost;
            ost << "Unhandled message at bottom of chain "
                << *msg << " (message type "
                << msg->getType().getName()
                << "). "
                << vespalib::getStackTrace(0);
            if (!msg->getType().isReply()) {
                //if (!_closed) {
                LOGBP(warning, "%s", ost.str().c_str());
                //}
                StorageCommand& cmd = static_cast<StorageCommand&>(*msg);
                shared_ptr<StorageReply> reply(cmd.makeReply().release());

                if (reply.get()) {
                    reply->setResult(ReturnCode(ReturnCode::NOT_IMPLEMENTED,
                        msg->getType().getName()));
                    sendUp(reply);
                }
            } else {
                ost << " Return code: "
                    << static_cast<StorageReply&>(*msg).getResult();
                //if (!_closed) {
                LOGBP(warning, "%s", ost.str().c_str());
                //}
            }
        //}
    } else if (!_down->onDown(msg)) {
        //LOG(spam, "Storage link %s forwarding message %s.",
        //    toString().c_str(), msg->toString().c_str());
        _down->sendDown(msg);
    } else {
        LOG(spam, "Storage link %s handled message %s.",
            _down->toString().c_str(), msg->toString().c_str());
    }
}

void StorageLink::sendUp(const shared_ptr<StorageMessage> & msg)
{
        // Verify acceptable state to send messages up
    switch(_state) {
        case OPENED:
        case CLOSING:
        case FLUSHINGDOWN:
        case FLUSHINGUP:
            break;
        default:
            LOG(error,
                "Link %s trying to send %s up while in state %s",
                toString().c_str(),
                msg->toString(true).c_str(),
                stateToString(_state));
            assert(false);
    }
    assert(msg.get());
    if (isTop()) {
        /*
        if (isFlush(msg)) {
            StorageCommand& cmd = static_cast<StorageCommand&>(*msg);
            shared_ptr<StorageReply> reply(cmd.makeReply().release());

            if (reply.get()) {
                sendDown(reply);
            }
        } else {
        */
            ostringstream ost;
            ost << "Unhandled message at top of chain " << *msg << ".";
            ost << vespalib::getStackTrace(0);
            if (!msg->getType().isReply()) {
                //if (!_closed) {
                LOGBP(warning, "%s", ost.str().c_str());
                //}
                StorageCommand& cmd = static_cast<StorageCommand&>(*msg);
                shared_ptr<StorageReply> reply(cmd.makeReply().release());

                if (reply.get()) {
                    reply->setResult(ReturnCode(ReturnCode::NOT_IMPLEMENTED,
                        msg->getType().getName()));
                    sendDown(reply);
                }
            } else {
                ost << " Return code: "
                    << static_cast<StorageReply&>(*msg).getResult();
                //if (!_closed) {
                LOGBP(warning, "%s", ost.str().c_str());
                //}
            }
        //}
    } else if (!_up->onUp(msg)) {
        _up->sendUp(msg);
    }
}

void StorageLink::printChain(std::ostream& out, std::string indent) const {
    out << indent << "StorageChain(" << size();
    if (!isTop()) out << ", not top";
    out << ")";
    const StorageLink* lastlink = _up;
    for (const StorageLink* link = this; link != 0; link = link->_down.get()) {
        out << "\n";
        link->print(out, false, indent + "  ");
        if (link->_up != lastlink) out << ", broken linkage";
        lastlink = link;
    }
}

bool StorageLink::onDown(const shared_ptr<StorageMessage> & msg)
{
    //LOG(spam, "Checking if storage link %s handles %s.",
    //    toString().c_str(), msg->toString().c_str());
    bool result = msg->callHandler(*this, msg);
    /*
    if (result) {
        LOG(spam, "Storage link %s handled message %s.",
            toString().c_str(), msg->toString().c_str());
    } else {
        LOG(spam, "Storage link %s did not handle message %s.",
            toString().c_str(), msg->toString().c_str());
    }
    */
    return result;
}

bool StorageLink::onUp(const shared_ptr<StorageMessage> & msg)
{
    return msg->callHandler(*this, msg);
}

const char*
StorageLink::stateToString(State state)
{
    switch (state) {
    case CREATED:
        return "CREATED";
    case OPENED:
        return "OPENED";
    case CLOSING:
        return "CLOSING";
    case FLUSHINGDOWN:
        return "FLUSHINGDOWN";
    case FLUSHINGUP:
        return "FLUSHINGUP";
    case CLOSED:
        return "CLOSED";
    default:
        assert(false);
        return 0;
    }
}
