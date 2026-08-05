/*
  ----------------------------------------------------------------------------
  File:        ump_transport_coremidi.mm
  Project:     UMP_io  (macOS backend)

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>
  ----------------------------------------------------------------------------

  CoreMIDI backend for the portable ump::Transport interface. Uses the macOS 11+
  UMP APIs (MIDIEventList family, kMIDIProtocol_2_0). Only this file touches
  CoreMIDI; consumers see ump_transport.hpp's neutral interface.
*/
#import <CoreFoundation/CoreFoundation.h>
#import <CoreMIDI/CoreMIDI.h>

#include "ump/ump_transport.hpp"

#include <cstring>
#include <map>
#include <mutex>
#include <vector>

namespace ump
{
namespace
{

std::string cfToStd(CFStringRef s)
{
    if (!s) return {};
    CFIndex len = CFStringGetLength(s);
    CFIndex cap = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::string out((size_t)cap, '\0');
    std::string result;
    if (CFStringGetCString(s, &out[0], cap, kCFStringEncodingUTF8))
        result.assign(out.c_str());
    return result;
}

std::string endpointName(MIDIEndpointRef ep)
{
    CFStringRef name = nullptr;
    if (MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &name) == noErr && name)
    {
        std::string s = cfToStd(name);
        CFRelease(name);
        return s;
    }
    return {};
}

Protocol endpointProtocol(MIDIEndpointRef ep)
{
    SInt32 p = 0;
    if (MIDIObjectGetIntegerProperty(ep, kMIDIPropertyProtocolID, &p) == noErr)
    {
        if (p == kMIDIProtocol_2_0) return Protocol::Midi2;
        if (p == kMIDIProtocol_1_0) return Protocol::Midi1;
    }
    return Protocol::Unknown;   // property absent -> not a native-UMP endpoint
}

uint64_t endpointId(MIDIEndpointRef ep)
{
    SInt32 uid = 0;
    MIDIObjectGetIntegerProperty(ep, kMIDIPropertyUniqueID, &uid);
    return (uint64_t)(uint32_t)uid;
}

// First Source/Destination whose display name matches `name` (0 if none).
MIDIEndpointRef findByName(Direction dir, const std::string &name)
{
    const bool      src = (dir == Direction::Source);
    const ItemCount n   = src ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; ++i)
    {
        MIDIEndpointRef ep = src ? MIDIGetSource(i) : MIDIGetDestination(i);
        if (ep && endpointName(ep) == name) return ep;
    }
    return 0;
}

// An open MIDIInputPort; disposing it disconnects sources and stops the block.
class CoreMIDIInputPort : public InputPort
{
public:
    explicit CoreMIDIInputPort(MIDIPortRef p) : port_(p) {}
    ~CoreMIDIInputPort() override { if (port_) MIDIPortDispose(port_); }

private:
    MIDIPortRef port_;
};

// An open MIDIOutputPort bound to a destination; disposing frees the port.
class CoreMIDIOutputPort : public OutputPort
{
public:
    CoreMIDIOutputPort(MIDIPortRef port, MIDIEndpointRef dest) : port_(port), dest_(dest) {}
    ~CoreMIDIOutputPort() override { if (port_) MIDIPortDispose(port_); }

    bool send(const uint32_t *words, size_t count) override
    {
        if (!port_ || !dest_ || count == 0) return false;
        MIDIEventList    list;
        MIDIEventPacket *pkt = MIDIEventListInit(&list, kMIDIProtocol_2_0);
        pkt = MIDIEventListAdd(&list, sizeof(list), pkt, /*time now*/ 0,
                               (ByteCount)count, words);
        if (!pkt) return false;   // e.g. count exceeds one packet (64 words)
        return MIDISendEventList(port_, dest_, &list) == noErr;
    }

private:
    MIDIPortRef     port_;
    MIDIEndpointRef dest_;
};

class CoreMIDITransport : public Transport
{
public:
    explicit CoreMIDITransport(const std::string &clientName)
    {
        CFStringRef cn = CFStringCreateWithCString(nullptr, clientName.c_str(), kCFStringEncodingUTF8);
        // Notify block fires on a CoreMIDI thread when the studio configuration
        // changes; we forward it so the consumer re-enumerates (hotplug).
        MIDIClientCreateWithBlock(cn ? cn : CFSTR("UMP_io"), &client_, ^(const MIDINotification *msg) {
            switch (msg->messageID)
            {
                case kMIDIMsgSetupChanged:
                case kMIDIMsgObjectAdded:
                case kMIDIMsgObjectRemoved:
                    this->notifyListeners();
                    break;
                default:
                    break;
            }
        });
        if (cn) CFRelease(cn);
    }

    ~CoreMIDITransport() override
    {
        if (client_) MIDIClientDispose(client_);
    }

    std::vector<EndpointInfo> endpoints(Direction dir, Protocol filter) override
    {
        std::vector<EndpointInfo> out;
        const bool     src = (dir == Direction::Source);
        const ItemCount n  = src ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
        for (ItemCount i = 0; i < n; ++i)
        {
            MIDIEndpointRef ep = src ? MIDIGetSource(i) : MIDIGetDestination(i);
            if (!ep) continue;
            Protocol proto = endpointProtocol(ep);
            if (filter != Protocol::Unknown && proto != filter) continue;
            out.push_back({ endpointName(ep), endpointId(ep), proto, dir });
        }
        return out;
    }

    // Open a protocol-2.0 input port on the named Source; UMP arrives in the
    // receive block (a CoreMIDI thread) and is forwarded to onRx per packet.
    std::unique_ptr<InputPort> openInput(const std::string &name, RxCallback onRx) override
    {
        MIDIEndpointRef src = findByName(Direction::Source, name);
        if (!src) return nullptr;

        RxCallback  cb   = std::move(onRx);   // copied into the block
        MIDIPortRef port = 0;
        OSStatus    st   = MIDIInputPortCreateWithProtocol(
            client_, CFSTR("UMP_io Input"), kMIDIProtocol_2_0, &port,
            ^(const MIDIEventList *evtlist, void * /*srcRefCon*/) {
                const MIDIEventPacket *p = &evtlist->packet[0];
                for (UInt32 i = 0; i < evtlist->numPackets; ++i)
                {
                    if (cb && p->wordCount) cb(p->words, p->wordCount);
                    p = MIDIEventPacketNext(p);
                }
            });
        if (st != noErr || !port) return nullptr;

        if (MIDIPortConnectSource(port, src, nullptr) != noErr)
        {
            MIDIPortDispose(port);
            return nullptr;
        }
        return std::unique_ptr<InputPort>(new CoreMIDIInputPort(port));
    }

    // Open an output port to the named Destination. UMP words are sent as a
    // MIDIEventList (protocol 2.0) via OutputPort::send.
    std::unique_ptr<OutputPort> openOutput(const std::string &name) override
    {
        MIDIEndpointRef dest = findByName(Direction::Destination, name);
        if (!dest) return nullptr;
        MIDIPortRef port = 0;
        if (MIDIOutputPortCreate(client_, CFSTR("UMP_io Output"), &port) != noErr || !port)
            return nullptr;
        return std::unique_ptr<OutputPort>(new CoreMIDIOutputPort(port, dest));
    }

    int addEndpointsChangedListener(EndpointsChangedCallback cb) override
    {
        std::lock_guard<std::mutex> lk(listenersMutex_);
        int id = nextListenerId_++;
        listeners_.emplace(id, std::move(cb));
        return id;
    }

    void removeEndpointsChangedListener(int id) override
    {
        std::lock_guard<std::mutex> lk(listenersMutex_);
        listeners_.erase(id);
    }

private:
    // Called from the CoreMIDI notify thread. Copy under lock, invoke outside it.
    void notifyListeners()
    {
        std::vector<EndpointsChangedCallback> cbs;
        {
            std::lock_guard<std::mutex> lk(listenersMutex_);
            cbs.reserve(listeners_.size());
            for (auto &kv : listeners_) cbs.push_back(kv.second);
        }
        for (auto &cb : cbs) if (cb) cb();
    }

    MIDIClientRef                           client_ = 0;
    std::mutex                              listenersMutex_;
    std::map<int, EndpointsChangedCallback> listeners_;
    int                                     nextListenerId_ = 1;
};

} // namespace

std::unique_ptr<Transport> createTransport(const std::string &clientName)
{
    return std::unique_ptr<Transport>(new CoreMIDITransport(clientName));
}

} // namespace ump
