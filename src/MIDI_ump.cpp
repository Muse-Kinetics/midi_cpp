/**
 * @file MIDI_ump.cpp
 * @brief Portable USB MIDI 2.0 / UMP Endpoint engine. See MIDI_ump.hpp.
 *
 * Entire translation unit is compiled out when ENABLE_MIDI2 is not defined.
 *
 * UMP words are little-endian on the USB wire (USB MIDI 2.0 §3.2.2), which is the
 * native byte order on little-endian MCUs, so no byte swapping is required here.
 */
#ifdef ENABLE_MIDI2

#include "MIDI_ump.hpp"

// Per-product identity (manufacturer/family/model + firmware version) lives in
// MIDI_CPP's device_metadata / MIDI_CPP_config. Enter that header chain via
// device_metadata (not MIDI_CPP_config directly) to satisfy its include ordering.
#include "MIDI_device_metadata.hpp" // kmi_id_*, kmi_family_*, PID_MIDI_MSB, getMidiProductId(), SYX_ID_APP_VER*

#include "umpMessageCreate.h"
#include <cstring>

namespace
{
    // ---- UMP Stream Endpoint Discovery filter bitmap (M2-104 UMP spec) --------
    constexpr uint8_t EP_FILTER_INFO       = 0x01;
    constexpr uint8_t EP_FILTER_DEVICEINFO = 0x02;
    constexpr uint8_t EP_FILTER_NAME       = 0x04;
    constexpr uint8_t EP_FILTER_PRODID     = 0x08;
    constexpr uint8_t EP_FILTER_STREAMCFG  = 0x10;

    // ---- Function Block Discovery filter bitmap ------------------------------
    constexpr uint8_t FB_FILTER_INFO = 0x01;
    constexpr uint8_t FB_FILTER_NAME = 0x02;

    // ---- Endpoint Info Notification: "Static Function Blocks" bit (word1 D31) -
    constexpr uint32_t EP_INFO_STATIC_FB = (uint32_t)1 << 31;

    // ---- Function Block Info fields common to all blocks (per-block group/dir/
    //      name come from the UMP_FunctionBlock array) --------------------------
    constexpr uint8_t FB_INDEX_ALL          = 0xFF;  // FB Discovery "all blocks"
    constexpr uint8_t FB_MIDICI_SUPPORT     = 0;     // no MIDI-CI yet (set in M5)
    constexpr uint8_t FB_IS_MIDI1           = 0;     // MIDI 2.0 protocol
    constexpr uint8_t FB_MAX_SYSEX8_STREAMS = 0;

    // ---- Stream Configuration protocol value (Workbench shows "protocol : 2") -
    constexpr uint8_t STREAM_PROTOCOL_MIDI2 = 0x02;

    // ---- Endpoint Info CVM capability flags ----------------------------------
    constexpr bool SUPPORTS_MIDI2_CVM = true;
    constexpr bool SUPPORTS_MIDI1_CVM = false;
    constexpr bool SUPPORTS_JR_RX     = false;
    constexpr bool SUPPORTS_JR_TX     = false;

    // UMP Endpoint text carries 14 chars/packet; Function Block name 13 chars/packet.
    constexpr uint8_t EP_TEXT_CHARS_PER_MSG = 14;
    constexpr uint8_t FB_NAME_CHARS_PER_MSG = 13;

    constexpr uint8_t  UMP_WORDS_MAX     = 4;   // largest UMP message = 128-bit
    constexpr uint8_t  UMP_BYTES_PER_WORD = 4;

    // UMP words-per-message from the Message Type nibble (M2-104 §3.2.3).
    uint8_t umpWordCount(uint8_t mt)
    {
        switch (mt)
        {
            case 0x0: case 0x1: case 0x2: case 0x6: case 0x7: return 1;   // 32-bit
            case 0x3: case 0x4: case 0x8: case 0x9: case 0xA: return 2;   // 64-bit
            case 0xB: case 0xC:                               return 3;   // 96-bit
            default:                                          return 4;   // 128-bit (0x5,0xD,0xE,0xF)
        }
    }
} // anonymous namespace

// ----------------------------------------------------------------------------

UMP_Endpoint::UMP_Endpoint() {}

void UMP_Endpoint::init(UMPEmitFn emit, uint16_t maxPacketSize)
{
    emit_          = emit;
    maxPacketSize_ = (maxPacketSize < UMP_MAX_PACKET) ? maxPacketSize : UMP_MAX_PACKET;

    ump_.setMidiEndpoint([this](uint8_t majVer, uint8_t minVer, uint8_t filter)
                         { onEndpointDiscovery(majVer, minVer, filter); });
    ump_.setFunctionBlock([this](uint8_t fbIdx, uint8_t filter)
                          { onFunctionBlock(fbIdx, filter); });
    ump_.setStreamConfigRequest([this](uint8_t protocol, bool jrrx, bool jrtx)
                                { onStreamConfigRequest(protocol, jrrx, jrtx); });
    // Channel-voice / SysEx callbacks are wired in M4 / M5.
}

void UMP_Endpoint::onRxBytes(const uint8_t *buf, uint8_t len)
{
    for (uint8_t i = 0; (uint16_t)(i + UMP_BYTES_PER_WORD) <= len; i += UMP_BYTES_PER_WORD)
    {
        uint32_t w = (uint32_t)buf[i]
                   | ((uint32_t)buf[i + 1] << 8)
                   | ((uint32_t)buf[i + 2] << 16)
                   | ((uint32_t)buf[i + 3] << 24);
        uint16_t next = (uint16_t)((rxHead_ + 1) % RX_FIFO_WORDS);
        if (next == rxTail_)
        {
            break;   // FIFO full: drop (host re-requests)
        }
        rxFifo_[rxHead_] = w;
        rxHead_ = next;
    }
}

void UMP_Endpoint::poll()
{
    while (rxTail_ != rxHead_)
    {
        uint32_t w = rxFifo_[rxTail_];
        rxTail_ = (uint16_t)((rxTail_ + 1) % RX_FIFO_WORDS);
        ump_.processUMP(w);
    }
    if (txLen_ > 0)
    {
        flushTx();
    }
}

// Append a UMP message (nWords) to the outbound accumulator, little-endian
// (LSB first per USB MIDI 2.0 §3.2.2 — native byte order on this LE MCU).
void UMP_Endpoint::queueUMP(const uint32_t *words, uint8_t nWords)
{
    if (txLen_ + (uint16_t)(nWords * UMP_BYTES_PER_WORD) > TX_BUF_BYTES)
    {
        return;   // overrun: drop (host re-requests / re-sends)
    }
    for (uint8_t w = 0; w < nWords; w++)
    {
        uint32_t word = words[w];
        txBuf_[txLen_++] = (uint8_t)(word & 0xFF);
        txBuf_[txLen_++] = (uint8_t)((word >> 8) & 0xFF);
        txBuf_[txLen_++] = (uint8_t)((word >> 16) & 0xFF);
        txBuf_[txLen_++] = (uint8_t)((word >> 24) & 0xFF);
    }
}

// ---- MIDI 2.0 Channel Voice TX (MT 0x4) ------------------------------------

void UMP_Endpoint::sendNoteOn(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity)
{
    std::array<uint32_t, 2> m = UMPMessage::mt4NoteOn(group, channel, note, velocity, 0, 0);
    queueUMP(m.data(), 2);
}

void UMP_Endpoint::sendNoteOff(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity)
{
    std::array<uint32_t, 2> m = UMPMessage::mt4NoteOff(group, channel, note, velocity, 0, 0);
    queueUMP(m.data(), 2);
}

void UMP_Endpoint::sendPolyPressure(uint8_t group, uint8_t channel, uint8_t note, uint32_t pressure)
{
    std::array<uint32_t, 2> m = UMPMessage::mt4CPolyPressure(group, channel, note, pressure);
    queueUMP(m.data(), 2);
}

// Bridge a MIDI 1.0 channel-voice message to MT 0x4 UMP, scaling 7-bit data to
// MIDI 2.0 resolution (M2Utils::scaleUp() implements the M2-104 spec scaling).
void UMP_Endpoint::sendMIDI1ChannelVoice(uint8_t status, uint8_t d1, uint8_t d2, uint8_t group)
{
    uint8_t channel = status & 0x0F;
    std::array<uint32_t, 2> m;

    switch (status & 0xF0)
    {
        case NOTE_OFF:
            sendNoteOff(group, channel, d1, (uint16_t)M2Utils::scaleUp(d2, 7, 16));
            return;
        case NOTE_ON:
            sendNoteOn(group, channel, d1, (uint16_t)M2Utils::scaleUp(d2, 7, 16));
            return;
        case KEY_PRESSURE:   // Poly Key Pressure (0xA0)
            sendPolyPressure(group, channel, d1, M2Utils::scaleUp(d2, 7, 32));
            return;
        case CC:
            m = UMPMessage::mt4CC(group, channel, d1, M2Utils::scaleUp(d2, 7, 32));
            break;
        case CHANNEL_PRESSURE:
            m = UMPMessage::mt4ChannelPressure(group, channel, M2Utils::scaleUp(d1, 7, 32));
            break;
        case PITCH_BEND:
            m = UMPMessage::mt4PitchBend(group, channel, M2Utils::scaleUp((uint16_t)(d1 | (d2 << 7)), 14, 32));
            break;
        case PROGRAM_CHANGE:
            m = UMPMessage::mt4ProgramChange(group, channel, d1, false, 0, 0);
            break;
        default:
            return;   // unsupported status: drop
    }
    queueUMP(m.data(), 2);
}

// Flush the accumulator, packing whole UMP messages into <= maxPacketSize_ USB
// packets. Keeps the unsent remainder for the next poll() if the transport is busy.
void UMP_Endpoint::flushTx()
{
    uint16_t off = 0;
    uint8_t  pkt[UMP_MAX_PACKET];

    while (off < txLen_)
    {
        uint16_t pl = 0;
        while (off + pl < txLen_)
        {
            // Message byte length from the MT nibble of word0 (byte off+pl+3, LE).
            uint8_t  mt   = (uint8_t)(txBuf_[off + pl + 3] >> 4);
            uint16_t mlen = (uint16_t)(umpWordCount(mt) * UMP_BYTES_PER_WORD);
            if (pl + mlen > maxPacketSize_)
            {
                break;   // packet full: send what we have (stays message-aligned)
            }
            pl += mlen;
        }
        memcpy(pkt, &txBuf_[off], pl);
        if (emit_ == nullptr || !emit_(pkt, pl))
        {
            break;   // busy / no transport: keep remaining bytes, retry next poll()
        }
        off += pl;
    }

    if (off > 0)
    {
        if (off < txLen_)
        {
            memmove(txBuf_, &txBuf_[off], (size_t)(txLen_ - off));
        }
        txLen_ -= off;
    }
}

// Endpoint Name / Product Instance Id -> UMP Endpoint Text Notifications.
void UMP_Endpoint::sendEndpointText(uint16_t replyType, const char *text, uint8_t len)
{
    uint8_t offset = 0;
    do
    {
        queueUMP(UMPMessage::mtFMidiEndpointTextNotify(replyType, offset, (uint8_t *)text, len), UMP_WORDS_MAX);
        offset += EP_TEXT_CHARS_PER_MSG;
    } while (offset < len);
}

// Function Block Name -> UMP Function Block Name Notifications.
void UMP_Endpoint::sendFunctionBlockName(uint8_t fbIdx, const char *text, uint8_t len)
{
    uint8_t offset = 0;
    do
    {
        queueUMP(UMPMessage::mtFFunctionBlockNameNotify(fbIdx, offset, (uint8_t *)text, len), UMP_WORDS_MAX);
        offset += FB_NAME_CHARS_PER_MSG;
    } while (offset < len);
}

// ---- UMP Stream request callbacks (device replies) -------------------------

void UMP_Endpoint::onEndpointDiscovery(uint8_t majVer, uint8_t minVer, uint8_t filter)
{
    (void)majVer;
    (void)minVer;

    if (filter & EP_FILTER_INFO)
    {
        std::array<uint32_t, 4> info = UMPMessage::mtFMidiEndpointInfoNotify(
            fbCount_, SUPPORTS_MIDI2_CVM, SUPPORTS_MIDI1_CVM, SUPPORTS_JR_RX, SUPPORTS_JR_TX);
        if (fbStatic_)
        {
            info[1] |= EP_INFO_STATIC_FB;   // Static Function Blocks bit (word1 D31)
        }
        queueUMP(info, UMP_WORDS_MAX);
    }
    if (filter & EP_FILTER_DEVICEINFO)
    {
        std::array<uint8_t, 3> manuId   = { kmi_id_1, kmi_id_2, kmi_id_3 };
        std::array<uint8_t, 2> familyId = { kmi_family_lsb, kmi_family_msb };
        std::array<uint8_t, 2> modelId  = { getMidiProductId(), PID_MIDI_MSB };
        std::array<uint8_t, 4> version  = { SYX_ID_APP_VER1, SYX_ID_APP_VER2,
                                            SYX_ID_APP_VER3, SYX_ID_APP_VER4 };
        queueUMP(UMPMessage::mtFMidiEndpointDeviceInfoNotify(manuId, familyId, modelId, version), UMP_WORDS_MAX);
    }
    if (filter & EP_FILTER_NAME)
    {
        sendEndpointText(MIDIENDPOINT_NAME_NOTIFICATION, endpointName_, (uint8_t)strlen(endpointName_));
    }
    if (filter & EP_FILTER_PRODID)
    {
        sendEndpointText(MIDIENDPOINT_PRODID_NOTIFICATION, productInstanceId_, (uint8_t)strlen(productInstanceId_));
    }
    if (filter & EP_FILTER_STREAMCFG)
    {
        queueUMP(UMPMessage::mtFNotifyProtocol(STREAM_PROTOCOL_MIDI2, SUPPORTS_JR_RX, SUPPORTS_JR_TX), UMP_WORDS_MAX);
    }
}

// Note: AM_MIDI2.0Lib's setFunctionBlock() setter names its parameters
// (filter, fbIdx), but umpProcessor.cpp invokes the callback as
// functionBlock(fbIdx, filter) — so the first argument is the Function Block
// index and the second is the filter bitmap. Match the actual call order.
void UMP_Endpoint::onFunctionBlock(uint8_t fbIdx, uint8_t filter)
{
    // fbIdx 0xFF requests all Function Blocks; otherwise a single index.
    uint8_t first = (fbIdx == FB_INDEX_ALL) ? 0 : fbIdx;
    uint8_t last  = (fbIdx == FB_INDEX_ALL) ? fbCount_ : (uint8_t)(fbIdx + 1);

    for (uint8_t i = first; i < last && i < fbCount_; i++)
    {
        if (filter & FB_FILTER_INFO)
        {
            sendFunctionBlockInfo(i);
        }
        if (filter & FB_FILTER_NAME)
        {
            sendFunctionBlockName(i, fbs_[i].name, (uint8_t)strlen(fbs_[i].name));
        }
    }
}

void UMP_Endpoint::sendFunctionBlockInfo(uint8_t fbIdx)
{
    const UMP_FunctionBlock &fb = fbs_[fbIdx];
    bool recv   = (fb.direction & UMP_FB_INPUT_ONLY)  != 0;   // has IN Group Terminals
    bool sender = (fb.direction & UMP_FB_OUTPUT_ONLY) != 0;   // has OUT Group Terminals
    queueUMP(UMPMessage::mtFFunctionBlockInfoNotify(fbIdx, /*active*/ true, fb.direction,
                                                    sender, recv, fb.firstGroup, fb.numGroups,
                                                    FB_MIDICI_SUPPORT, FB_IS_MIDI1,
                                                    FB_MAX_SYSEX8_STREAMS), UMP_WORDS_MAX);
}

void UMP_Endpoint::onStreamConfigRequest(uint8_t protocol, bool jrrx, bool jrtx)
{
    (void)protocol;
    // We operate the MIDI 2.0 protocol on Group 0; acknowledge accordingly.
    queueUMP(UMPMessage::mtFNotifyProtocol(STREAM_PROTOCOL_MIDI2, jrrx, jrtx), UMP_WORDS_MAX);
}

#endif // ENABLE_MIDI2
