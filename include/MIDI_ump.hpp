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
 * M2 scope: UMP Stream Endpoint/Function Block Discovery. Channel-voice (M4) and
 * MIDI-CI (M5) callbacks are added later. See .buddy-project/MIDI2_development.md.
 */
#ifndef MIDI_UMP_HPP
#define MIDI_UMP_HPP

#ifdef ENABLE_MIDI2

#include <cstdint>
#include <cstddef>
#include <array>
#include "umpProcessor.h"

/// Transport emit callback: send `len` bytes of an already-packed UMP packet
/// (little-endian words). Return true if accepted/queued, false if the transport
/// is busy — the engine keeps the remainder and retries on the next poll().
typedef bool (*UMPEmitFn)(const uint8_t *bytes, uint16_t len);

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

class UMP_Endpoint
{
public:
    UMP_Endpoint();

    /// Identity strings are borrowed (not copied) — keep them alive for the
    /// endpoint's lifetime. Safe to change at runtime (e.g. a user-renamed FB).
    void setEndpointName(const char *name)      { endpointName_ = name; }
    void setProductInstanceId(const char *pid)  { productInstanceId_ = pid; }

    /// Declare the endpoint's Function Blocks (array borrowed, not copied).
    void setFunctionBlocks(const UMP_FunctionBlock *blocks, uint8_t count)
    { fbs_ = blocks; fbCount_ = count; }

    /// Advertise Function Blocks as static (won't change after discovery).
    void setStaticFunctionBlocks(bool isStatic) { fbStatic_ = isStatic; }

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
    void sendNoteOn(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity);
    void sendNoteOff(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity);
    void sendPolyPressure(uint8_t group, uint8_t channel, uint8_t note, uint32_t pressure);

    /// Bridge a MIDI 1.0 channel-voice message (status + up to 2 data bytes) to an
    /// MT 0x4 UMP on the given group, scaling 7-bit values to MIDI 2.0 resolution.
    void sendMIDI1ChannelVoice(uint8_t status, uint8_t d1, uint8_t d2, uint8_t group);

private:
    void onEndpointDiscovery(uint8_t majVer, uint8_t minVer, uint8_t filter);
    void onFunctionBlock(uint8_t fbIdx, uint8_t filter);
    void onStreamConfigRequest(uint8_t protocol, bool jrrx, bool jrtx);

    void queueUMP(const uint32_t *words, uint8_t nWords);
    template <std::size_t N>
    void queueUMP(const std::array<uint32_t, N> &m, uint8_t nWords) { queueUMP(m.data(), nWords); }
    void flushTx();
    void sendEndpointText(uint16_t replyType, const char *text, uint8_t len);
    void sendFunctionBlockInfo(uint8_t fbIdx);
    void sendFunctionBlockName(uint8_t fbIdx, const char *text, uint8_t len);

    umpProcessor ump_;
    UMPEmitFn    emit_          = nullptr;
    uint16_t     maxPacketSize_ = 0;

    const char *endpointName_      = "";
    const char *productInstanceId_ = "";

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

    // Outbound UMP byte accumulator (whole messages; flushed in poll()).
    static const uint16_t TX_BUF_BYTES = 256;
    uint8_t  txBuf_[TX_BUF_BYTES];
    uint16_t txLen_ = 0;
};

#endif // ENABLE_MIDI2

#endif // MIDI_UMP_HPP
