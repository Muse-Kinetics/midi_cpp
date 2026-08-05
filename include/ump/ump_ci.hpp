/*
  ----------------------------------------------------------------------------
  File:        ump_ci.hpp
  Project:     UMP_io

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>
  ----------------------------------------------------------------------------

  Portable MIDI-CI / UMP-Stream "device host" (initiator) — the shared session
  API behind the Max UMP_device/UMP_prop/UMP_profile objects AND the future
  VST3/AU plugin. Framework-neutral: no Max / CoreMIDI / host types — only UTF-8
  strings, plain structs, and std::function callbacks (same rules as
  ump_transport.hpp). The implementation (T2+) wraps AM_MIDI2.0Lib for CI/UMP-
  Stream message create/parse and rides ump::Transport for I/O.

  Design: .buddy-project/device-host-design.md (decision D11).
  Status: INTERFACE ONLY — no implementation yet. Defines the T2..T5 target.

  Two info channels are unified here (see the design doc):
    * UMP Stream (MT 0xF): endpoint + Function Block topology (no SysEx).
    * MIDI-CI (SysEx8):     Discovery, Property Exchange, Profiles (per-MUID).
*/
#ifndef UMP_CI_HPP
#define UMP_CI_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "ump/ump_transport.hpp"   // ump::Transport for the underlying UMP I/O

namespace ump { namespace ci {

// ---- data ------------------------------------------------------------------

enum class SessionStatus { Idle, Discovering, Ready, Timeout, Gone };

struct DeviceIdentity {
    uint8_t  manufacturer[3] = {0,0,0};   // SysEx manufacturer ID
    uint16_t family   = 0;
    uint16_t model    = 0;
    uint32_t revision = 0;
};

// A UMP Function Block (how the Hub exposes kick/snare/cymbals within one endpoint).
struct FunctionBlock {
    uint8_t     index      = 0;
    std::string name;
    uint8_t     direction  = 0;   // 1 = input, 2 = output, 3 = bidirectional
    uint8_t     firstGroup = 0;   // 0-15
    uint8_t     numGroups  = 1;
    bool        active     = true;
};

struct EndpointInfo {
    std::string                name;               // UMP Stream Endpoint Name
    std::string                productInstanceId;
    DeviceIdentity             identity;
    uint8_t                    umpVersionMajor = 0;
    uint8_t                    umpVersionMinor = 0;
    std::vector<FunctionBlock> functionBlocks;
    // MIDI-CI:
    uint32_t                   muid           = 0; // remote MUID (0 = unknown)
    bool                       supportsPE       = false;
    bool                       supportsProfiles = false;
};

// One Property Exchange resource as declared in ResourceList.
struct ResourceEntry {
    std::string resource;                     // e.g. "X-TriggerSettings", "DeviceInfo"
    bool        canGet       = true;
    bool        canSet       = false;
    bool        canSubscribe = false;
    std::string schemaJson;                   // inline JSON Schema, if the device ships one
};

// A MIDI-CI Profile (e.g. MPE, Drums) and where it is targeted.
struct Profile {
    uint8_t     id[5] = {0,0,0,0,0};          // 5-byte Profile ID
    std::string name;                         // friendly name if known
    bool        enabled = false;
    uint8_t     group   = 0;                  // target group (0-15)
    uint8_t     channel = 0x7F;               // target channel (0-15, or 0x7F = whole group/FB)
};

// ---- callbacks -------------------------------------------------------------
// All async replies are delivered on the Transport's callback thread; the host
// (Max qelem / plugin FIFO) marshals — the core does not (matches D3).

using StatusCb    = std::function<void(SessionStatus)>;
using EndpointCb  = std::function<void(const EndpointInfo&)>;
using JsonReplyCb = std::function<void(bool ok, const std::string& json, const std::string& error)>;
using ResourcesCb = std::function<void(const std::vector<ResourceEntry>&)>;
using SubscribeCb = std::function<void(const std::string& resource, const std::string& json)>;
using ProfilesCb  = std::function<void(const std::vector<Profile>&)>;

// ---- session (one device / MUID) -------------------------------------------

class Session {
public:
    virtual ~Session() = default;

    virtual const std::string&  deviceName() const = 0;   // the endpoint name it was opened by
    virtual SessionStatus       status()     const = 0;
    virtual const EndpointInfo& endpoint()   const = 0;

    // Thread-safe snapshots (copy under the session lock) for hosts reading from a
    // different thread than the transport RX callback (e.g. a Max qelem).
    virtual EndpointInfo         endpointCopy() = 0;
    virtual std::vector<Profile> profilesCopy() = 0;

    // Kick off UMP-Stream + MIDI-CI discovery. Async: watch onStatusChanged /
    // onEndpointChanged. Safe to call again to re-discover.
    virtual void discover() = 0;

    // ---- Property Exchange (client) ----
    virtual void resourceList(ResourcesCb cb) = 0;
    // `path` is an optional JSON path within the resource ("" = whole resource).
    virtual void getProperty(const std::string& resource, const std::string& path, JsonReplyCb cb) = 0;
    virtual void setProperty(const std::string& resource, const std::string& path,
                             const std::string& json, JsonReplyCb cb) = 0;
    virtual int  subscribe(const std::string& resource, SubscribeCb cb) = 0;   // -> sub id (>=0)
    virtual void unsubscribe(int subId) = 0;

    // Poll-friendly PE — for hosts that read on a different thread than the reply
    // (e.g. a Max clock/qelem). requestProperty fires a GET and caches the reply;
    // takeProperty pops a fresh reply (true once per new reply). No callback into
    // the host, so no host-lifetime coupling.
    virtual void requestProperty(const std::string& resource) = 0;
    virtual bool takeProperty(const std::string& resource, std::string& jsonOut) = 0;
    virtual void requestSet(const std::string& resource, const std::string& json) = 0;
    // Poll-friendly subscribe: device-pushed updates land in the same cache as
    // requestProperty, so takeProperty() pops them. No host-lifetime coupling.
    virtual void subscribeProperty(const std::string& resource) = 0;
    virtual void unsubscribeProperty(const std::string& resource) = 0;

    // ---- Profiles (client) ----
    virtual void inquireProfiles(ProfilesCb cb) = 0;
    virtual void enableProfile(const uint8_t id[5], uint8_t group, uint8_t channel, bool on) = 0;

    // ---- events ----
    virtual void onStatusChanged(StatusCb cb)     = 0;
    virtual void onEndpointChanged(EndpointCb cb) = 0;
    virtual void onProfileChanged(ProfilesCb cb)  = 0;
};

// ---- host (registry of sessions by endpoint name) --------------------------

class Host {
public:
    virtual ~Host() = default;

    // Open (or return the existing) session for an endpoint name. Shared so that
    // multiple frontend objects (UMP_prop/UMP_profile @device "name") and the
    // plugin reuse one session. Begins discovery.
    virtual std::shared_ptr<Session> open(const std::string& endpointName) = 0;

    // Existing session for a name, or nullptr — for @device lookups.
    virtual std::shared_ptr<Session> find(const std::string& endpointName) = 0;

    // Endpoints that answered MIDI-CI Discovery (the UMP_device browser).
    virtual std::vector<std::string> ciCapableEndpoints() = 0;
};

// The host drives its own UMP I/O over the given transport (control plane), so it
// can coexist with UMP_in/UMP_out on the same endpoints (data plane).
std::unique_ptr<Host> createHost(Transport& transport, const std::string& hostName);

/// Process-global host with its own transport (control plane). The Max
/// UMP_device / UMP_prop / UMP_profile objects share this so `@device "name"`
/// resolves to one session per device. Lazily created on first use.
Host& defaultHost();

}} // namespace ump::ci

#endif // UMP_CI_HPP
