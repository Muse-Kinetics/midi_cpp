/*
  ----------------------------------------------------------------------------
  File:        MIDI_ump.hpp

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>

  Portions built on AM_MIDI2.0Lib (Andrew Mee), MIT License.
  ----------------------------------------------------------------------------
*/
/**
 * @file MIDI_ump.hpp
 * @brief Portable USB MIDI 2.0 / UMP Endpoint engine (built on AM_MIDI2.0Lib).
 *
 * Transport-agnostic and product-agnostic:
 *   - The application injects a UMP-emit callback + the maximum transport packet
 *     size (init), and supplies the Endpoint/Function-Block name and Product
 *     Instance Id (setters).
 *   - Device identity (manufacturer / family / model / version) is taken from
 *     MIDI_CPP's device_metadata + MIDI_CPP_config — the same per-product identity
 *     used by the MIDI 1.0 SysEx Device Inquiry reply.
 *
 * The whole engine compiles out unless ENABLE_MIDI2 is defined. AM_MIDI2.0Lib is
 * an *optional* dependency: every include of it below is guarded by ENABLE_MIDI2,
 * so MIDI_CPP builds with AM_MIDI2.0Lib entirely absent when the flag is off.
 * NOTE for consuming projects: set `lib_ldf_mode = chain+` in platformio.ini so
 * PlatformIO's dependency finder honours these #ifdef guards (plain `chain` mode
 * text-scans includes and would demand AM_MIDI2.0Lib even with the flag off).
 *
 * Data flow (driven by the host application):
 *   - onRxBytes(): called from the transport RX path with raw UMP bytes; enqueues.
 *   - poll(): drains inbound UMP through the processor and flushes replies via emit.
 *
 * Scope: UMP Stream Endpoint/Function Block Discovery, MIDI 2.0 Channel Voice TX,
 * and MIDI-CI Capability Inquiry — Discovery, Profile Inquiry, and Property
 * Exchange (foundational resources + a declarative/product-registered resource
 * table with Get/Set). MIDI 1.0 is unaffected when ENABLE_MIDI2 is undefined.
 */
#ifndef MIDI_UMP_HPP
#define MIDI_UMP_HPP

#ifdef ENABLE_MIDI2

#include <cstdint>
#include <cstddef>
#include <array>
#include "umpProcessor.h"
#include "midiCIProcessor.h"   // MIDI-CI (Capability Inquiry) on the Function Block

/// Transport emit callback: send `len` bytes of an already-packed UMP packet
/// (little-endian words). Return true if accepted/queued, false if the transport
/// is busy — the engine keeps the remainder and retries on the next poll().
typedef bool (*UMPEmitFn)(const uint8_t *bytes, uint16_t len);

/// Entropy source for the MIDI-CI MUID. The app supplies a function returning a
/// fresh 32-bit random value (hardware-seeded); the engine masks it to 28 bits.
/// Required by Common Rules for MIDI-CI: the MUID must be regenerated each power
/// cycle and must not repeat across restarts. If unset, the engine falls back to
/// a *stable* MUID derived from the Product Instance Id (fails that rule — set it).
typedef uint32_t (*UMPRandFn)(void);

/// UMP Function Block direction (M2-104 §7.1.8).
enum UMP_FB_Direction : uint8_t
{
    UMP_FB_INPUT_ONLY  = 0x01,
    UMP_FB_OUTPUT_ONLY = 0x02,
    UMP_FB_BIDIRECTIONAL = 0x03,
};

/// One UMP Function Block declared by this endpoint. `name` is borrowed.
struct UMP_FunctionBlock
{
    uint8_t     firstGroup;   ///< 0-15
    uint8_t     numGroups;    ///< 1-16, contiguous
    uint8_t     direction;    ///< UMP_FB_Direction
    const char *name;
};

/// Backing data type of a declarative Property Exchange field.
enum UMP_PEType : uint8_t
{
    PE_U8, PE_U16, PE_U32, PE_I8, PE_I16, PE_I32, PE_FLOAT, PE_BOOL, PE_STR
};

/// One field of a declarative PE resource. The JSON key lives here and nowhere
/// else — the engine reads/writes `ptr` (by `type`) for Get/Set and derives the
/// resource's JSON Schema from this table. For integer types, when `min != max`
/// the engine clamps incoming Set values to [min,max]. `size` is the buffer
/// capacity for PE_STR (ignored otherwise). Rows may omit the trailing
/// min/max/size (they default to 0 = unbounded / n/a).
struct UMP_PEField
{
    const char *name;      ///< JSON key
    void       *ptr;       ///< backing storage (borrowed)
    UMP_PEType  type;
    int32_t     min;       ///< clamp lower bound (min==max -> no clamp)
    int32_t     max;       ///< clamp upper bound
    uint16_t    size;      ///< buffer capacity for PE_STR
};

/// Fired once after a resource's fields are applied on a Set. Return false to
/// reject the Set (-> status 400). `ctx` is the resource's registered ctx. This
/// is where a product does RAM-derived recompute / LED refresh / persistence.
typedef bool (*UMP_PEApplyFn)(void *ctx);

/// Escape-hatch callbacks for irregular resources (arrays, computed strings)
/// that don't fit the flat field table. Used only when `fields == nullptr`.
///  - get: write the JSON body into out[0..cap); return length or -1.
///  - set: consume the JSON body; return a PE status code (200 on success).
typedef int (*UMP_PEGetFn)(char *out, int cap, void *ctx);
typedef int (*UMP_PESetFn)(const char *body, int len, void *ctx);

/// A product-specific Property Exchange resource. Products register these; the
/// engine serves the foundational resources (ResourceList/DeviceInfo/ChannelList)
/// itself. All pointers are borrowed. Two forms:
///   1. Declarative (preferred): supply `fields`; the engine auto-serialises Get,
///      auto-applies Set (when `writable`), and auto-derives the JSON Schema.
///   2. Escape hatch: leave `fields` null and supply `get`/`set`; then `schema`
///      must be provided for manufacturer-specific ("X-") resources.
struct UMP_PEResource
{
    const char        *name;        ///< resource name ("X-TriggerSettings")
    const char        *title;       ///< human title (used for the auto-schema)
    const UMP_PEField *fields;      ///< declarative table, or nullptr for escape hatch
    uint8_t            fieldCount;
    bool               writable;    ///< field-based + writable -> canSet:"full"
    UMP_PEApplyFn      onApplied;   ///< optional post-Set hook (nullptr ok)
    void              *ctx;         ///< passed to onApplied / get / set
    UMP_PEGetFn        get;         ///< escape-hatch Get (used iff fields==nullptr)
    UMP_PESetFn        set;         ///< escape-hatch Set (used iff fields==nullptr)
    const char        *schema;      ///< explicit schema; else derived from fields
};

class UMP_Endpoint;   // forward declaration for the profile callbacks below

/// A MIDI-CI Profile the device supports. The library implements the generic
/// Common-Rules-for-Profiles envelope (Profile Inquiry/list, Set Profile On/Off,
/// Enabled/Disabled notifications, and routing of Profile Details Inquiry and
/// Profile Specific Data); the product supplies the 5-byte Profile ID and the
/// profile-specific behaviour via these callbacks. The library never interprets a
/// profile's payload — Details/Specific-Data bytes pass through opaquely. All
/// callbacks are optional (nullptr): a profile with none still enables/disables
/// and appears in the Profile Inquiry list. Registered array is borrowed.
struct UMP_Profile
{
    uint8_t  id[5];                                       ///< 5-byte Profile ID
    /// Device ID where this profile lives (MIDI-CI Common Rules §2.3): a MIDI
    /// channel 0x00-0x0F (single/multi-channel profile), 0x7E (group), or 0x7F
    /// (function block). The engine reports/enables the profile at this address
    /// and only lists it in a Profile Inquiry addressed to it.
    uint8_t  address;
    /// Enable on `numChannels`; return channels actually enabled (<= requested,
    /// 0 = refuse). If null, enables with the requested channel count.
    uint8_t (*onEnable)(uint8_t numChannels, void *ctx);
    void    (*onDisable)(void *ctx);
    /// Profile Details Inquiry: write reply bytes for `target` into out[0..cap);
    /// return length, or -1 if the target is unsupported (empty reply sent).
    int     (*onDetailsInquiry)(uint8_t target, uint8_t *out, int cap, void *ctx);
    /// Profile Specific Data: opaque inbound payload. Respond (0..N times) by
    /// calling ep->replyProfileData(). `part`/`last` reflect chunked inbound data.
    void    (*onSpecificData)(UMP_Endpoint *ep, const uint8_t *data, uint16_t len,
                              uint16_t part, bool last, void *ctx);
    void    *ctx;                                         ///< passed back to callbacks
};

class UMP_Endpoint
{
public:
    UMP_Endpoint();

    /// Identity strings are borrowed (not copied) — keep them alive for the
    /// endpoint's lifetime. Safe to change at runtime (e.g. a user-renamed FB).
    void setEndpointName(const char *name)      { endpointName_ = name; }
    void setProductInstanceId(const char *pid)  { productInstanceId_ = pid; }

    /// Human-readable manufacturer name for the Property Exchange DeviceInfo
    /// resource (the numeric manufacturerId comes from device metadata). Borrowed.
    void setManufacturerName(const char *name)  { manufacturerName_ = name; }

    /// Register product-specific Property Exchange resources (array borrowed).
    /// They are added to ResourceList automatically and served on Get.
    void setPEResources(const UMP_PEResource *res, uint8_t count)
    { peRes_ = res; peResCount_ = count; }

    /// Register the MIDI-CI Profiles this device supports (array borrowed). The
    /// Profile Configuration category is advertised in Discovery when count > 0.
    void setProfiles(const UMP_Profile *profiles, uint8_t count)
    { profiles_ = profiles; profileCount_ = count; }

    /// Send a Profile Specific Data reply for the profile currently being handled.
    /// Valid only from within a UMP_Profile::onSpecificData callback.
    void replyProfileData(const uint8_t *data, uint16_t len);

    /// Declare the endpoint's Function Blocks (array borrowed, not copied).
    void setFunctionBlocks(const UMP_FunctionBlock *blocks, uint8_t count)
    { fbs_ = blocks; fbCount_ = count; }

    /// Advertise Function Blocks as static (won't change after discovery).
    void setStaticFunctionBlocks(bool isStatic) { fbStatic_ = isStatic; }

    /// Supply a hardware entropy source for the MIDI-CI MUID (see UMPRandFn).
    void setRandomSource(UMPRandFn fn) { randFn_ = fn; }

    /// Wire the transport and register the UMP Stream callbacks.
    void init(UMPEmitFn emit, uint16_t maxPacketSize);

    /// Feed raw UMP bytes from the transport RX (IRQ-safe: only enqueues words).
    void onRxBytes(const uint8_t *buf, uint8_t len);

    /// Drain inbound UMP, dispatch through the processor, flush outbound replies.
    /// Call once per main-loop pass.
    void poll();

    // ---- MIDI 2.0 Channel Voice TX (MT 0x4). Values are full MIDI 2.0 -------
    //      resolution (16-bit velocity, 32-bit controllers). Queued like any
    //      other outbound UMP and flushed by poll().
    //
    // Note On/Off carry an optional Attribute (type + 16-bit data): 0x00 none
    // (default), 0x03 Pitch 7.9, or a Profile-defined type such as the Drums
    // Profile's 0x20 (4-bit gesture | 12-bit distance-from-center). A receiver
    // that does not understand the attribute type ignores the attribute data.
    void sendNoteOn(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity,
                    uint8_t attrType = 0, uint16_t attrData = 0);
    void sendNoteOff(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity,
                     uint8_t attrType = 0, uint16_t attrData = 0);
    void sendPolyPressure(uint8_t group, uint8_t channel, uint8_t note, uint32_t pressure);

    /// Registered Per-Note Controller (MT 0x4). `index` is the RPNC number (e.g.
    /// the Drums Profile's 0x74 Position Angle / 0x73 Distance / 0x70-0x72), and
    /// `value` is the full 32-bit controller value. Per MIDI 2.0, may be sent
    /// before the Note On to pre-arm the value for the upcoming note.
    void sendPerNoteRPN(uint8_t group, uint8_t channel, uint8_t note, uint8_t index, uint32_t value);

    /// Per-Note Pitch Bend (MT 0x4), 32-bit centred at 0x80000000.
    void sendPerNotePitchBend(uint8_t group, uint8_t channel, uint8_t note, uint32_t value);

    /// Bridge a MIDI 1.0 channel-voice message (status + up to 2 data bytes) to an
    /// MT 0x4 UMP on the given group, scaling 7-bit values to MIDI 2.0 resolution.
    void sendMIDI1ChannelVoice(uint8_t status, uint8_t d1, uint8_t d2, uint8_t group);

private:
    void onEndpointDiscovery(uint8_t majVer, uint8_t minVer, uint8_t filter);
    void onFunctionBlock(uint8_t fbIdx, uint8_t filter);
    void onStreamConfigRequest(uint8_t protocol, bool jrrx, bool jrtx);

    // ---- MIDI-CI (M5) — Capability Inquiry, generic + Profile-pluggable ------
    void     initCI();                                    ///< wire the CI processor + callbacks
    void     ensureMUID();                                ///< lazily generate the MUID on first use
    uint32_t makeMUID() const;                            ///< draw a fresh 28-bit MUID (random if seeded)
    bool     ciCheckMUID(uint8_t group, uint32_t muid);   ///< is this MUID addressed to us?
    void     routeSysEx7(const umpData &mess);            ///< UMP SysEx7 -> CI processor (form-aware)
    void     onCIDiscovery(const MIDICI &ci, uint16_t peerMaxSysex); ///< Discovery -> Discovery Reply
    void     onCIInvalidateMUID(uint32_t terminateMuid);  ///< drop our MUID -> regenerate next time
    // ---- MIDI-CI Profile Configuration (generic Common-Rules envelope) -------
    void     onCIProfileInquiry(const MIDICI &ci);        ///< Profile Inquiry -> registered-profile list
    void     onCIProfileOn(const MIDICI &ci, const uint8_t id[5], uint8_t numChannels);
    void     onCIProfileOff(const MIDICI &ci, const uint8_t id[5]);
    void     onCIProfileDetailsInquiry(const MIDICI &ci, const uint8_t id[5], uint8_t target);
    void     onCIProfileSpecificData(const MIDICI &ci, const uint8_t id[5],
                                     const uint8_t *data, uint16_t len, uint16_t part, bool last);
    int8_t   profileIndex(const uint8_t id[5]) const;     ///< registered index, or -1
    void     onCIPECapabilities(const MIDICI &ci);        ///< PE Capabilities -> Reply
    // ---- Property Exchange (M5 step 5) ----
    void     onCIPEGetInquiry(const MIDICI &ci, const char *header, uint16_t len); ///< PE Get -> resource dispatch
    void     onCIPESetInquiry(const MIDICI &ci, const char *header, uint16_t headerLen,
                              const uint8_t *body, uint16_t bodyLen, bool lastByteOfSet); ///< PE Set (reassemble + apply)
    void     sendPEReply(const MIDICI &ci, uint16_t status, const uint8_t *body, uint16_t bodyLen); ///< PE Get Reply (1 chunk)
    void     sendPESetReplyStatus(const MIDICI &ci, uint16_t status); ///< PE Set Reply (status header only)
    const UMP_PEResource *findPEResource(const char *name, uint16_t nameLen); ///< product-table lookup
    int      buildDeviceInfoJSON(char *out, int cap);     ///< foundational resource: DeviceInfo
    int      buildChannelListJSON(char *out, int cap);    ///< foundational resource: ChannelList
    int      buildResourceListJSON(char *out, int cap);   ///< foundational resource: ResourceList (+ product)
    void     sendCISysex(uint8_t group, const uint8_t *body, uint16_t len); ///< pack CI bytes as SysEx7 UMP

    void queueUMP(const uint32_t *words, uint8_t nWords);
    template <std::size_t N>
    void queueUMP(const std::array<uint32_t, N> &m, uint8_t nWords) { queueUMP(m.data(), nWords); }
    void flushTx();
    void sendEndpointText(uint16_t replyType, const char *text, uint8_t len);
    void sendFunctionBlockInfo(uint8_t fbIdx);
    void sendFunctionBlockName(uint8_t fbIdx, const char *text, uint8_t len);

    umpProcessor    ump_;
    midiCIProcessor ci_;
    uint32_t        localMUID_    = 0;      ///< this Function Block's MUID (valid when muidValid_)
    uint16_t        peerMaxSysex_ = 512;    ///< initiator's max receivable SysEx (from Discovery); chunk replies to fit
    bool            muidValid_    = false;  ///< MUID generated this session?
    bool            ciInProgress_ = false;  ///< a MIDI-CI SysEx7 is mid-reassembly
    UMPRandFn       randFn_        = nullptr;
    UMPEmitFn       emit_          = nullptr;
    uint16_t        maxPacketSize_ = 0;

    const char *endpointName_      = "";
    const char *productInstanceId_ = "";
    const char *manufacturerName_  = "";

    const UMP_PEResource *peRes_     = nullptr;   ///< product PE resources (borrowed)
    uint8_t               peResCount_ = 0;

    // ---- MIDI-CI Profiles (borrowed table + engine-managed enabled state) ----
    static const uint8_t  UMP_MAX_PROFILES = 4;
    const UMP_Profile    *profiles_     = nullptr;
    uint8_t               profileCount_ = 0;
    uint8_t               profileChannels_[UMP_MAX_PROFILES] = { 0 }; ///< 0 = disabled, else channels
    // "Current" profile context for replyProfileData(), set during a Specific-Data callback.
    uint8_t               curProfileId_[5]      = { 0 };
    uint8_t               curProfileCiVer_      = 2;
    uint32_t              curProfileRemoteMUID_ = 0;
    uint8_t               curProfileGroup_      = 0;
    uint8_t               curProfileDeviceId_   = 0x7F;  ///< Device ID (address) of the in-flight profile transaction

    // Property Exchange scratch (single-chunk). peJson_ builds a resource body
    // (or the ResourceList, which now carries auto-derived schemas); peSysex_ is
    // the assembled CI SysEx; peSetBuf_ reassembles an inbound Set body across
    // the CI processor's <=256-byte slices. Sized so the multi-field settable
    // resources + their ResourceList schemas fit one message (advertised max
    // SysEx below). Larger payloads would need real PE chunking (future).
    char        peJson_[1280];
    uint8_t     peSysex_[1280];
    uint8_t     peSetBuf_[1024];
    uint16_t    peSetLen_ = 0;

    const UMP_FunctionBlock *fbs_     = nullptr;
    uint8_t                  fbCount_ = 0;
    bool                     fbStatic_ = true;

    // Largest transport packet this engine will build on the stack (USB FS bulk).
    static const uint16_t UMP_MAX_PACKET = 64;

    // Inbound UMP word FIFO (transport-IRQ producer -> poll() consumer).
    static const uint16_t RX_FIFO_WORDS = 64;
    volatile uint32_t rxFifo_[RX_FIFO_WORDS];
    volatile uint16_t rxHead_ = 0;
    volatile uint16_t rxTail_ = 0;

    // Outbound UMP byte accumulator (whole messages; flushed in poll()). Must
    // hold the largest single reply before flush: a max PE chunk (512-byte CI
    // SysEx) packs to ~683 SysEx7 UMP bytes (8 bytes per 6 payload bytes), so
    // this is sized to 1 KB. flushTx() drains it across poll() passes as the USB
    // TX ring frees, but the whole message must fit here first.
    static const uint16_t TX_BUF_BYTES = 2048;
    uint8_t  txBuf_[TX_BUF_BYTES];
    uint16_t txLen_ = 0;
};

#endif // ENABLE_MIDI2

#endif // MIDI_UMP_HPP
