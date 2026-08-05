/*
  ----------------------------------------------------------------------------
  File:        ump_transport.hpp
  Project:     UMP_io

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>
  ----------------------------------------------------------------------------
*/
/**
 * @file ump_transport.hpp
 * @brief Framework-neutral Universal MIDI Packet (UMP) transport interface.
 *
 * This is the portable core shared by every UMP_io consumer: the Max externals
 * (UMP_in/UMP_out) today, and a cross-platform VST3/AU plugin later. It exposes
 * ONLY portable types — UMP 32-bit words, UTF-8 names, stable ids, and
 * std::function callbacks. No platform headers (CoreMIDI / Windows MIDI
 * Services) and no host headers (Max SDK / plugin SDK) appear here, so a single
 * backend serves every consumer.
 *
 * Backends implement this interface:
 *   - macOS  : ump_transport_coremidi.mm  (CoreMIDI MIDIEventList / protocol 2.0)
 *   - Windows: ump_transport_wms.cpp      (Windows MIDI Services SDK)  [planned]
 *
 * Threading contract: RxCallback and EndpointsChangedCallback are invoked on a
 * backend/OS thread, NOT the consumer's thread. The consumer marshals to its own
 * context (a Max scheduler clock, or a plugin's lock-free FIFO). The core stays
 * marshaling-agnostic so it can be reused by hosts with different threading.
 */
#ifndef UMP_TRANSPORT_HPP
#define UMP_TRANSPORT_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ump
{

/// UMP protocol an endpoint negotiated (reported by the OS: CoreMIDI ProtocolID,
/// or WMS). Used to filter to native MIDI 2.0 (UMP) endpoints.
enum class Protocol : uint8_t
{
    Unknown = 0,
    Midi1   = 1,
    Midi2   = 2,
};

/// Data flow relative to *this* host: a Source is read (input), a Destination is
/// written (output) — matching CoreMIDI's source/destination split.
enum class Direction : uint8_t
{
    Source,
    Destination,
};

/// One discovered endpoint. `name` is the user-facing display name that UMP_in/
/// UMP_out match on (like Max's midiin/midiout port argument); `id` is a stable
/// backend handle used to reconnect the same endpoint across hotplug.
struct EndpointInfo
{
    std::string name;
    uint64_t    id        = 0;
    Protocol    protocol  = Protocol::Unknown;
    Direction   direction = Direction::Source;
};

/// Inbound UMP delivery. `words`/`count` is one contiguous run of complete UMP
/// packets (1-4 words each). Invoked on a backend thread — marshal before
/// touching the host.
using RxCallback = std::function<void(const uint32_t *words, size_t count)>;

/// Endpoint set changed (device connected/disconnected). Invoked on a backend
/// thread — the consumer re-enumerates and updates its port list.
using EndpointsChangedCallback = std::function<void()>;

/// An open input connection. Destroying it disconnects.
class InputPort
{
public:
    virtual ~InputPort() = default;
};

/// An open output connection. Destroying it disconnects.
class OutputPort
{
public:
    virtual ~OutputPort() = default;
    /// Send one or more complete UMP packets. Returns false on transport error.
    virtual bool send(const uint32_t *words, size_t count) = 0;
};

/// A platform MIDI transport (one per host client). Create with createTransport().
class Transport
{
public:
    virtual ~Transport() = default;

    /// Current endpoints for a direction. `filter` limits by protocol
    /// (Protocol::Midi2 -> only native MIDI 2.0 UMP endpoints; Unknown -> all).
    virtual std::vector<EndpointInfo> endpoints(Direction dir,
                                                Protocol  filter = Protocol::Unknown) = 0;

    /// Open the first Source whose display name equals `name`. UMP is delivered
    /// to `onRx` on a backend thread. Returns null if no match.
    virtual std::unique_ptr<InputPort> openInput(const std::string &name, RxCallback onRx) = 0;

    /// Open the first Destination whose display name equals `name`. Returns null
    /// if no match.
    virtual std::unique_ptr<OutputPort> openOutput(const std::string &name) = 0;

    /// Register a hotplug listener (endpoint added/removed/setup changed).
    /// Returns an id for removeEndpointsChangedListener(). Multiple listeners are
    /// supported — each consumer (e.g. every UMP_in/UMP_out instance) registers its
    /// own. Listeners fire on a backend thread; marshal before touching the host.
    virtual int  addEndpointsChangedListener(EndpointsChangedCallback cb) = 0;
    virtual void removeEndpointsChangedListener(int id) = 0;
};

/// Build the platform backend (CoreMIDI on Apple, Windows MIDI Services on
/// Windows). `clientName` names this MIDI client to the OS.
std::unique_ptr<Transport> createTransport(const std::string &clientName);

} // namespace ump

#endif // UMP_TRANSPORT_HPP
