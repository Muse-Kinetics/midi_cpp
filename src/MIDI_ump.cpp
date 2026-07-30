/*
  ----------------------------------------------------------------------------
  File:        MIDI_ump.cpp

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>

  Portions built on AM_MIDI2.0Lib (Andrew Mee), MIT License.
  ----------------------------------------------------------------------------
*/
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
#include "midiCIMessageCreate.h"   // CIMessage::send* Capability Inquiry builders
#include <cstring>
#include <cstdio>                  // snprintf (Property Exchange JSON bodies)
#include <cstdlib>                 // strtol / strtod (Restricted-JSON value readers)

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
    constexpr uint8_t FB_MIDICI_SUPPORT     = 2;     // MIDI-CI message version format 1.2 (M5)
    constexpr uint8_t FB_IS_MIDI1           = 0;     // MIDI 2.0 protocol
    constexpr uint8_t FB_MAX_SYSEX8_STREAMS = 0;

    // ---- MIDI-CI (M5) --------------------------------------------------------
    // Common Rules for MIDI-CI require Max SysEx Size >= 512 whenever Profile
    // Configuration or Property Exchange is advertised. We advertise more headroom
    // (1024) so the ResourceList — which now carries an auto-derived JSON Schema
    // per settable field-based resource — and the multi-field Get/Set bodies fit a
    // single PE message. Payloads beyond this would need real PE chunking (future).
    constexpr uint32_t CI_MAX_SYSEX    = 1024;
    constexpr uint8_t  CI_OUTPUT_PATH  = 0;
    constexpr uint8_t  CI_FB_INDEX     = 0;     // Function Block that owns Capability Inquiry
    // Capability Inquiry "Category Supported" bitmap (Discovery, M2-101 §Discovery).
    // Verified against the MIDI2.0Workbench decoder: 0x04 = Profile Configuration,
    // 0x08 = Property Exchange, 0x10 = Process Inquiry. Bits 0x01/0x02 are reserved
    // (Protocol Negotiation was 0x01 in CI 1.1, deprecated in 1.2 — protocol is
    // negotiated via the UMP Stream Configuration path in M2).
    //  - Profile Configuration must NOT be advertised until at least one Profile
    //    exists (a Profile-Inquiry reply must list >=1 Profile). Drums Profile
    //    registers in M6; OR in CI_CAT_PROFILE then.
    constexpr uint8_t  CI_CAT_PROFILE   = 0x04;   // (advertised starting M6)
    constexpr uint8_t  CI_CAT_PROPERTY  = 0x08;
    constexpr uint8_t  CI_CAT_PROCESS   = 0x10;   // (unused)
    constexpr uint8_t  CI_CATEGORIES    = CI_CAT_PROPERTY;
    // Property Exchange capabilities (handshake only in M5; resources land in M5 step 5).
    constexpr uint8_t  PE_SIMUL_REQUESTS = 1;
    constexpr uint8_t  PE_MAJ_VER = 0;
    constexpr uint8_t  PE_MIN_VER = 0;
    constexpr uint16_t PE_STATUS_OK = 200;

    // PE reply chunking. On-wire SysEx message = F0 + fixed CI/PE fields + header
    // + body + F7; the fixed overhead (everything but header/body) is 24 bytes.
    // A reply body larger than one message is split across multiple PE messages
    // (same requestId, incrementing "Number of This Chunk"; the header appears in
    // chunk 1 only). Each chunk's wire size is capped at min(initiator max SysEx,
    // our build budget) so it fits both the receiver and our peSysex_ buffer.
    constexpr uint16_t PE_MSG_FIXED   = 24;    // F0..F7 framing + PE fixed fields
    constexpr uint16_t PE_OUT_MSG_CAP = 1024;  // per-chunk wire cap (<= peSysex_)

    // Extract the "resource" value span from a Restricted-JSON PE header
    // (Contribution 16: {"resource":"<name>"[,"option":...]}). Resolving the name
    // to a resource once here (§4.3) avoids any post-dispatch string scanning.
    bool peExtractName(const char *hdr, uint16_t len, const char **name, uint16_t *nameLen)
    {
        static const char tag[] = "\"resource\"";
        const uint16_t tagLen = 10;
        int p = -1;
        for (uint16_t i = 0; (uint16_t)(i + tagLen) <= len; i++)
        {
            if (memcmp(hdr + i, tag, tagLen) == 0) { p = (int)i + tagLen; break; }
        }
        if (p < 0) return false;
        while (p < (int)len && (hdr[p] == ':' || hdr[p] == ' ')) p++;
        if (p >= (int)len || hdr[p] != '"') return false;
        p++;                                    // past opening quote
        const char *v = hdr + p;
        uint16_t vlen = 0;
        while ((uint16_t)(p + vlen) < len && v[vlen] != '"') vlen++;
        *name = v;
        *nameLen = vlen;
        return true;
    }

    bool peNameIs(const char *name, uint16_t nameLen, const char *lit)
    {
        return strlen(lit) == (size_t)nameLen && memcmp(name, lit, nameLen) == 0;
    }

    // Copy a 5-byte Profile ID into the std::array the AM_MIDI2 builders expect.
    std::array<uint8_t, 5> idArray(const uint8_t id[5])
    {
        std::array<uint8_t, 5> a{};
        for (uint8_t i = 0; i < 5; i++) a[i] = id[i];
        return a;
    }

    // ---- Generic Restricted-JSON value readers (product-agnostic) -----------
    // Locate `"key"` then return a pointer just past the following ':'. nullptr
    // if the key is absent. Skips spaces. Not a full JSON parser — sufficient for
    // the flat objects Property Exchange resources use.
    const char *jsonValueAfterKey(const char *body, int len, const char *key)
    {
        int klen = (int)strlen(key);
        for (int i = 0; i + klen + 2 <= len; i++)
        {
            if (body[i] == '"' && memcmp(body + i + 1, key, klen) == 0 && body[i + 1 + klen] == '"')
            {
                int p = i + 2 + klen;
                while (p < len && (body[p] == ' ' || body[p] == '\t')) p++;
                if (p < len && body[p] == ':')
                {
                    p++;
                    while (p < len && (body[p] == ' ' || body[p] == '\t')) p++;
                    return body + p;
                }
            }
        }
        return nullptr;
    }

    bool jsonFindInt(const char *body, int len, const char *key, long *out)
    {
        const char *v = jsonValueAfterKey(body, len, key);
        if (v == nullptr) return false;
        char *end = nullptr;
        long n = strtol(v, &end, 10);
        if (end == v) return false;
        *out = n;
        return true;
    }

    bool jsonFindFloat(const char *body, int len, const char *key, double *out)
    {
        const char *v = jsonValueAfterKey(body, len, key);
        if (v == nullptr) return false;
        char *end = nullptr;
        double n = strtod(v, &end);
        if (end == v) return false;
        *out = n;
        return true;
    }

    bool jsonFindStr(const char *body, int len, const char *key, char *out, int cap)
    {
        const char *v = jsonValueAfterKey(body, len, key);
        if (v == nullptr || cap <= 0) return false;
        const char *endBody = body + len;
        if (v >= endBody || *v != '"') return false;
        v++;                                            // past opening quote
        int n = 0;
        while (v < endBody && *v != '"' && n < cap - 1) out[n++] = *v++;
        out[n] = '\0';
        return true;
    }

    // ---- Declarative field engine (product-agnostic) ------------------------
    long readFieldInt(const UMP_PEField &f)
    {
        switch (f.type)
        {
            case PE_U8:  return *(uint8_t  *)f.ptr;
            case PE_U16: return *(uint16_t *)f.ptr;
            case PE_U32: return (long)*(uint32_t *)f.ptr;
            case PE_I8:  return *(int8_t   *)f.ptr;
            case PE_I16: return *(int16_t  *)f.ptr;
            case PE_I32: return *(int32_t  *)f.ptr;
            case PE_BOOL:return *(bool     *)f.ptr ? 1 : 0;
            default:     return 0;
        }
    }

    void writeFieldInt(const UMP_PEField &f, long v)
    {
        if (f.min != f.max)                             // declarative clamp
        {
            if (v < f.min) v = f.min;
            if (v > f.max) v = f.max;
        }
        switch (f.type)
        {
            case PE_U8:  *(uint8_t  *)f.ptr = (uint8_t)v;  break;
            case PE_U16: *(uint16_t *)f.ptr = (uint16_t)v; break;
            case PE_U32: *(uint32_t *)f.ptr = (uint32_t)v; break;
            case PE_I8:  *(int8_t   *)f.ptr = (int8_t)v;   break;
            case PE_I16: *(int16_t  *)f.ptr = (int16_t)v;  break;
            case PE_I32: *(int32_t  *)f.ptr = (int32_t)v;  break;
            case PE_BOOL:*(bool     *)f.ptr = (v != 0);    break;
            default: break;
        }
    }

    // Serialize a field table to a JSON object. Returns length or -1 on overflow.
    int serializeFields(const UMP_PEField *fields, uint8_t n, char *out, int cap)
    {
        int p = 0;
        if (p + 1 >= cap) return -1;
        out[p++] = '{';
        for (uint8_t i = 0; i < n; i++)
        {
            const UMP_PEField &f = fields[i];
            const char *comma = i ? "," : "";
            int m;
            switch (f.type)
            {
                case PE_FLOAT: m = snprintf(out + p, cap - p, "%s\"%s\":%g", comma, f.name, (double)*(float *)f.ptr); break;
                case PE_BOOL:  m = snprintf(out + p, cap - p, "%s\"%s\":%s", comma, f.name, (*(bool *)f.ptr) ? "true" : "false"); break;
                case PE_STR:   m = snprintf(out + p, cap - p, "%s\"%s\":\"%s\"", comma, f.name, (const char *)f.ptr); break;
                default:       m = snprintf(out + p, cap - p, "%s\"%s\":%ld", comma, f.name, readFieldInt(f)); break;
            }
            if (m < 0 || m >= cap - p) return -1;
            p += m;
        }
        if (p + 2 > cap) return -1;
        out[p++] = '}';
        out[p]   = '\0';
        return p;
    }

    // Apply a Set body to a field table. Only keys present in the body are written
    // (partial payloads leave other fields untouched). Returns fields written.
    int applySetFields(const UMP_PEResource &r, const uint8_t *body, uint16_t len)
    {
        const char *b = (const char *)body;
        int applied = 0;
        for (uint8_t i = 0; i < r.fieldCount; i++)
        {
            const UMP_PEField &f = r.fields[i];
            if (f.type == PE_FLOAT)
            {
                double v;
                if (jsonFindFloat(b, len, f.name, &v)) { *(float *)f.ptr = (float)v; applied++; }
            }
            else if (f.type == PE_STR)
            {
                if (jsonFindStr(b, len, f.name, (char *)f.ptr, f.size)) applied++;
            }
            else
            {
                long v;
                if (jsonFindInt(b, len, f.name, &v)) { writeFieldInt(f, v); applied++; }
            }
        }
        return applied;
    }

    // JSON Schema "type" keyword for a field type.
    const char *jsonSchemaType(UMP_PEType t)
    {
        switch (t)
        {
            case PE_FLOAT: return "number";
            case PE_BOOL:  return "boolean";
            case PE_STR:   return "string";
            default:       return "integer";
        }
    }

    // Derive a JSON Schema object from a field table (single source of truth, so
    // it can never drift from Get/Set). Returns length or -1 on overflow.
    int buildSchemaFromFields(const char *title, const UMP_PEField *fields, uint8_t n, char *out, int cap)
    {
        int p = snprintf(out, cap, "{\"title\":\"%s\",\"type\":\"object\",\"properties\":{",
                         title ? title : "");
        if (p < 0 || p >= cap) return -1;
        for (uint8_t i = 0; i < n; i++)
        {
            int m = snprintf(out + p, cap - p, "%s\"%s\":{\"type\":\"%s\"}",
                             i ? "," : "", fields[i].name, jsonSchemaType(fields[i].type));
            if (m < 0 || m >= cap - p) return -1;
            p += m;
        }
        if (p + 3 > cap) return -1;
        out[p++] = '}';
        out[p++] = '}';
        out[p]   = '\0';
        return p;
    }
    // SysEx7 form nibble (M2-104 §4.2).
    constexpr uint8_t  SX7_COMPLETE  = 0x0;
    constexpr uint8_t  SX7_START     = 0x1;
    constexpr uint8_t  SX7_CONTINUE  = 0x2;
    constexpr uint8_t  SX7_END       = 0x3;
    constexpr uint8_t  SX7_MAX_BYTES = 6;
    // MIDI-CI SysEx framing bytes (see AM_MIDI2.0Lib utils.h: S7UNIVERSAL_NRT / S7MIDICI).
    constexpr uint8_t  CI_MIN_SYSEX_LEN = 3;    // 0x7E, deviceId, 0x0D

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
    initCI();   // MIDI-CI (Capability Inquiry) on the Function Block
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

void UMP_Endpoint::sendNoteOn(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity,
                             uint8_t attrType, uint16_t attrData)
{
    std::array<uint32_t, 2> m = UMPMessage::mt4NoteOn(group, channel, note, velocity, attrType, attrData);
    queueUMP(m.data(), 2);
}

void UMP_Endpoint::sendNoteOff(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity,
                              uint8_t attrType, uint16_t attrData)
{
    std::array<uint32_t, 2> m = UMPMessage::mt4NoteOff(group, channel, note, velocity, attrType, attrData);
    queueUMP(m.data(), 2);
}

void UMP_Endpoint::sendPolyPressure(uint8_t group, uint8_t channel, uint8_t note, uint32_t pressure)
{
    std::array<uint32_t, 2> m = UMPMessage::mt4CPolyPressure(group, channel, note, pressure);
    queueUMP(m.data(), 2);
}

void UMP_Endpoint::sendPerNoteRPN(uint8_t group, uint8_t channel, uint8_t note, uint8_t index, uint32_t value)
{
    std::array<uint32_t, 2> m = UMPMessage::mt4PerNoteRPN(group, channel, note, index, value);
    queueUMP(m.data(), 2);
}

void UMP_Endpoint::sendPerNotePitchBend(uint8_t group, uint8_t channel, uint8_t note, uint32_t value)
{
    std::array<uint32_t, 2> m = UMPMessage::mt4PerNotePitchBend(group, channel, note, value);
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

// ============================================================================
//  MIDI-CI (Capability Inquiry) — generic, Profile-pluggable (M5)
//  Runs on the single Function Block. Fed MIDI-CI SysEx from the UMP transport
//  (SysEx7), it answers Discovery, Profile Inquiry, and PE Capabilities. The
//  Profile registry and PE resources are intentionally empty here — Drums
//  Profile registers in M6 and PE resources arrive in M5 step 5.
// ============================================================================

// Draw a fresh 28-bit MUID. Common Rules for MIDI-CI require the MUID to be
// regenerated each power cycle and not repeat across restarts, so we pull from
// the app's hardware entropy source when one is set. Without it we fall back to a
// *stable* hash of the Product Instance Id (deterministic — fails that rule; the
// project is expected to call setRandomSource()).
uint32_t UMP_Endpoint::makeMUID() const
{
    uint32_t r;
    if (randFn_ != nullptr)
    {
        r = randFn_();
    }
    else
    {
        r = 2166136261u;                            // FNV-1a offset basis
        for (const char *p = productInstanceId_; p && *p; ++p)
        {
            r ^= (uint8_t)*p;
            r *= 16777619u;                         // FNV-1a prime
        }
    }
    uint32_t muid = r & 0x0FFFFFFFu;                // MUID is 28-bit
    if (muid == (uint32_t)M2_CI_BROADCAST)
    {
        muid ^= 0x1u;                               // never the broadcast MUID
    }
    return muid;
}

// Generate the MUID on first use rather than at boot, so the entropy source is
// sampled once the device has been running (live DWT/ADC noise) — this keeps the
// value reliably different across restarts.
void UMP_Endpoint::ensureMUID()
{
    if (!muidValid_)
    {
        localMUID_ = makeMUID();
        muidValid_ = true;
    }
}

void UMP_Endpoint::initCI()
{
    // MUID is generated lazily (ensureMUID) on the first Discovery, not here.

    ci_.setCheckMUID([this](uint8_t group, uint32_t muid, void *) { return ciCheckMUID(group, muid); });

    ci_.setRecvDiscovery([this](MIDICI ci,
                                std::array<uint8_t, 3>, std::array<uint8_t, 2>,
                                std::array<uint8_t, 2>, std::array<uint8_t, 4>,
                                uint8_t, uint16_t maxSysex, uint8_t)
                         { onCIDiscovery(ci, maxSysex); });

    ci_.setRecvInvalidateMUID([this](MIDICI, uint32_t terminateMuid) { onCIInvalidateMUID(terminateMuid); });

    ci_.setRecvProfileInquiry([this](MIDICI ci) { onCIProfileInquiry(ci); });
    ci_.setRecvProfileOn([this](MIDICI ci, std::array<uint8_t, 5> p, uint8_t ch) { onCIProfileOn(ci, p.data(), ch); });
    ci_.setRecvProfileOff([this](MIDICI ci, std::array<uint8_t, 5> p) { onCIProfileOff(ci, p.data()); });
    ci_.setRecvProfileDetailsInquiry([this](MIDICI ci, std::array<uint8_t, 5> p, uint8_t target)
                                     { onCIProfileDetailsInquiry(ci, p.data(), target); });
    ci_.setRecvProfileSpecificData([this](MIDICI ci, std::array<uint8_t, 5> p, uint16_t len, uint8_t *d,
                                          uint16_t part, bool last)
                                   { onCIProfileSpecificData(ci, p.data(), d, len, part, last); });

    ci_.setPECapabilities([this](MIDICI ci, uint8_t, uint8_t, uint8_t) { onCIPECapabilities(ci); });

    ci_.setRecvPEGetInquiry([this](MIDICI ci, std::string header)
                            { onCIPEGetInquiry(ci, header.c_str(), (uint16_t)header.size()); });

    ci_.setRecvPESetInquiry([this](MIDICI ci, std::string header, uint16_t bodyLen, uint8_t *body,
                                   bool /*lastByteOfChunk*/, bool lastByteOfSet)
                            { onCIPESetInquiry(ci, header.c_str(), (uint16_t)header.size(),
                                               body, bodyLen, lastByteOfSet); });

    // Route reassembled UMP SysEx7 into the CI processor.
    ump_.setSysEx([this](umpData mess) { routeSysEx7(mess); });
}

bool UMP_Endpoint::ciCheckMUID(uint8_t group, uint32_t muid)
{
    (void)group;                        // single Function Block, Group 0
    return muidValid_ && muid == localMUID_;
}

// A controller may ask us to drop our MUID (collision resolution). Invalidate it
// so a fresh one is generated on the next Discovery.
void UMP_Endpoint::onCIInvalidateMUID(uint32_t terminateMuid)
{
    if (muidValid_ && terminateMuid == localMUID_)
    {
        muidValid_ = false;
    }
}

// A SysEx7 UMP arrives one packet at a time with a form nibble (M2-104 §4.2):
// COMPLETE (single), START, CONTINUE, END. midiCIProcessor is a streaming byte
// parser, so we open on START/COMPLETE, feed bytes, and close on END/COMPLETE —
// but only for MIDI-CI SysEx (Universal Non-RT 0x7E, sub-ID 0x0D).
void UMP_Endpoint::routeSysEx7(const umpData &mess)
{
    if (mess.form == SX7_COMPLETE || mess.form == SX7_START)
    {
        ciInProgress_ = (mess.dataLength >= CI_MIN_SYSEX_LEN
                         && mess.data[0] == S7UNIVERSAL_NRT
                         && mess.data[2] == S7MIDICI);
        if (ciInProgress_)
        {
            uint8_t deviceId = (mess.dataLength >= 2) ? mess.data[1] : (uint8_t)FUNCTION_BLOCK;
            ci_.startSysex7(mess.umpGroup, deviceId);
        }
    }

    if (!ciInProgress_)
    {
        return;
    }

    for (uint8_t i = 0; i < mess.dataLength; i++)
    {
        ci_.processMIDICI(mess.data[i]);
    }

    if (mess.form == SX7_COMPLETE || mess.form == SX7_END)
    {
        ci_.endSysex7();
        ciInProgress_ = false;
    }
}

// Pack an already-built MIDI-CI SysEx body (0x7E ... last byte, no F0/F7) into
// SysEx7 UMP packets (6 bytes/packet) and queue them for flush.
void UMP_Endpoint::sendCISysex(uint8_t group, const uint8_t *body, uint16_t len)
{
    if (len == 0)
    {
        return;
    }
    uint16_t off = 0;
    do
    {
        uint8_t n = (uint8_t)((len - off) < SX7_MAX_BYTES ? (len - off) : SX7_MAX_BYTES);
        std::array<uint8_t, 6> sx = { 0, 0, 0, 0, 0, 0 };
        for (uint8_t i = 0; i < n; i++)
        {
            sx[i] = body[off + i];
        }
        bool    first = (off == 0);
        bool    last  = ((uint16_t)(off + n) >= len);
        uint8_t form  = first ? (last ? SX7_COMPLETE : SX7_START)
                              : (last ? SX7_END : SX7_CONTINUE);
        queueUMP(UMPMessage::mt3Sysex7(group, form, n, sx), 2);
        off += n;
    } while (off < len);
}

// Discovery -> Discovery Reply, advertising our identity + supported categories
// (Profile Configuration + Property Exchange). srcMUID is ours; destMUID is the
// initiator (ci.remoteMUID).
void UMP_Endpoint::onCIDiscovery(const MIDICI &ci, uint16_t peerMaxSysex)
{
    ensureMUID();   // first Discovery: pick this session's random MUID

    // Remember the initiator's receivable max SysEx so PE replies are chunked to
    // fit. Common Rules floor is 512; guard against a bogus/zero advertisement.
    peerMaxSysex_ = (peerMaxSysex >= 512) ? peerMaxSysex : 512;

    std::array<uint8_t, 3> manuId   = { kmi_id_1, kmi_id_2, kmi_id_3 };
    std::array<uint8_t, 2> familyId = { kmi_family_lsb, kmi_family_msb };
    std::array<uint8_t, 2> modelId  = { getMidiProductId(), PID_MIDI_MSB };
    std::array<uint8_t, 4> version  = { SYX_ID_APP_VER1, SYX_ID_APP_VER2,
                                        SYX_ID_APP_VER3, SYX_ID_APP_VER4 };

    // Advertise Profile Configuration only when >=1 Profile is registered (Common
    // Rules: a Profile-Inquiry reply must list >=1 Profile to claim the category).
    uint8_t categories = CI_CAT_PROPERTY;
    if (profileCount_ > 0) categories |= CI_CAT_PROFILE;

    uint8_t  sx[64];
    uint16_t len = CIMessage::sendDiscoveryReply(sx, ci.ciVer, localMUID_, ci.remoteMUID,
                                                 manuId, familyId, modelId, version,
                                                 categories, CI_MAX_SYSEX,
                                                 CI_OUTPUT_PATH, CI_FB_INDEX);
    sendCISysex(ci.umpGroup, sx, len);
}

// ---- MIDI-CI Profile Configuration (generic Common-Rules envelope) ---------
// The library runs this state machine for any registered profile; the product's
// UMP_Profile callbacks supply the profile-specific behaviour and payloads.

int8_t UMP_Endpoint::profileIndex(const uint8_t id[5]) const
{
    for (uint8_t i = 0; i < profileCount_; i++)
    {
        if (memcmp(profiles_[i].id, id, 5) == 0) return (int8_t)i;
    }
    return -1;
}

// Profile Inquiry -> Profile List Response, at the inquiry's address. Only the
// profiles that live at that address (§2.3) are listed, split into currently-
// enabled and currently-disabled from the registered table + engine state.
void UMP_Endpoint::onCIProfileInquiry(const MIDICI &ci)
{
    uint8_t enabled[5 * UMP_MAX_PROFILES];
    uint8_t disabled[5 * UMP_MAX_PROFILES];
    uint8_t ne = 0, nd = 0;
    for (uint8_t i = 0; i < profileCount_ && i < UMP_MAX_PROFILES; i++)
    {
        if (profiles_[i].address != ci.deviceId) continue;   // report each profile only at its address
        if (profileChannels_[i] > 0) { memcpy(enabled + ne * 5, profiles_[i].id, 5); ne++; }
        else                         { memcpy(disabled + nd * 5, profiles_[i].id, 5); nd++; }
    }
    uint8_t  sx[32 + 5 * UMP_MAX_PROFILES * 2];
    uint16_t len = CIMessage::sendProfileListResponse(sx, ci.ciVer, localMUID_, ci.remoteMUID,
                                                      ci.deviceId, ne, enabled, nd, disabled);
    sendCISysex(ci.umpGroup, sx, len);
}

// Set Profile On -> enable via the product callback, then notify Enabled/Disabled.
void UMP_Endpoint::onCIProfileOn(const MIDICI &ci, const uint8_t id[5], uint8_t numChannels)
{
    int8_t idx = profileIndex(id);
    if (idx < 0) return;   // unknown profile

    uint8_t ch = numChannels;
    if (profiles_[idx].onEnable != nullptr)
    {
        ch = profiles_[idx].onEnable(numChannels, profiles_[idx].ctx);
    }
    profileChannels_[idx] = ch;

    uint8_t  sx[32];
    uint16_t len = (ch > 0)
        ? CIMessage::sendProfileEnabled(sx, ci.ciVer, localMUID_, ci.remoteMUID, ci.deviceId, idArray(id), ch)
        : CIMessage::sendProfileDisabled(sx, ci.ciVer, localMUID_, ci.remoteMUID, ci.deviceId, idArray(id), 0);
    sendCISysex(ci.umpGroup, sx, len);
}

// Set Profile Off -> disable via the product callback, then notify Disabled.
void UMP_Endpoint::onCIProfileOff(const MIDICI &ci, const uint8_t id[5])
{
    int8_t idx = profileIndex(id);
    if (idx < 0) return;

    if (profiles_[idx].onDisable != nullptr)
    {
        profiles_[idx].onDisable(profiles_[idx].ctx);
    }
    profileChannels_[idx] = 0;

    uint8_t  sx[32];
    uint16_t len = CIMessage::sendProfileDisabled(sx, ci.ciVer, localMUID_, ci.remoteMUID,
                                                  ci.deviceId, idArray(id), 0);
    sendCISysex(ci.umpGroup, sx, len);
}

// Profile Details Inquiry -> Reply. The reply payload is profile-specific and
// supplied opaquely by the product callback (empty if unsupported).
void UMP_Endpoint::onCIProfileDetailsInquiry(const MIDICI &ci, const uint8_t id[5], uint8_t target)
{
    int8_t idx = profileIndex(id);
    if (idx < 0) return;

    uint8_t data[64];
    int     dl = 0;
    if (profiles_[idx].onDetailsInquiry != nullptr)
    {
        dl = profiles_[idx].onDetailsInquiry(target, data, (int)sizeof(data), profiles_[idx].ctx);
    }
    if (dl < 0) dl = 0;

    uint8_t  sx[96];
    uint16_t len = CIMessage::sendProfileDetailsReply(sx, ci.ciVer, localMUID_, ci.remoteMUID,
                                                      ci.deviceId, idArray(id), target, (uint16_t)dl, data);
    sendCISysex(ci.umpGroup, sx, len);
}

// Profile Specific Data -> hand the opaque payload to the product callback, which
// responds (0..N times) via replyProfileData(). The library does not interpret it.
void UMP_Endpoint::onCIProfileSpecificData(const MIDICI &ci, const uint8_t id[5],
                                           const uint8_t *data, uint16_t len, uint16_t part, bool last)
{
    int8_t idx = profileIndex(id);
    if (idx < 0 || profiles_[idx].onSpecificData == nullptr) return;

    memcpy(curProfileId_, id, 5);
    curProfileCiVer_      = ci.ciVer;
    curProfileRemoteMUID_ = ci.remoteMUID;
    curProfileGroup_      = ci.umpGroup;
    curProfileDeviceId_   = ci.deviceId;
    profiles_[idx].onSpecificData(this, data, len, part, last, profiles_[idx].ctx);
}

// Send a Profile Specific Data reply for the in-flight profile transaction.
void UMP_Endpoint::replyProfileData(const uint8_t *data, uint16_t len)
{
    uint16_t n = CIMessage::sendProfileSpecificData(peSysex_, curProfileCiVer_, localMUID_,
                                                    curProfileRemoteMUID_, curProfileDeviceId_,
                                                    idArray(curProfileId_), len, (uint8_t *)data);
    sendCISysex(curProfileGroup_, peSysex_, n);
}

// PE Capabilities Inquiry -> Reply (handshake). Advertises how many simultaneous
// PE requests we accept; the actual resources are served in M5 step 5.
void UMP_Endpoint::onCIPECapabilities(const MIDICI &ci)
{
    uint8_t  sx[32];
    uint16_t len = CIMessage::sendPECapabilityReply(sx, ci.ciVer, localMUID_, ci.remoteMUID,
                                                    PE_SIMUL_REQUESTS, PE_MAJ_VER, PE_MIN_VER);
    sendCISysex(ci.umpGroup, sx, len);
}

// ---- Property Exchange Get (M5 step 5) -------------------------------------

// Find a registered product resource by name span.
const UMP_PEResource *UMP_Endpoint::findPEResource(const char *name, uint16_t nameLen)
{
    for (uint8_t i = 0; i < peResCount_; i++)
    {
        if (peNameIs(name, nameLen, peRes_[i].name)) return &peRes_[i];
    }
    return nullptr;
}

// PE Get Inquiry: resolve the requested resource (foundational or product),
// build its JSON body, reply. Malformed header -> 400, unknown resource -> 404.
// Bodies here fit a single PE chunk; larger payloads will need PE chunking.
void UMP_Endpoint::onCIPEGetInquiry(const MIDICI &ci, const char *header, uint16_t len)
{
    const char *name    = nullptr;
    uint16_t    nameLen = 0;
    if (!peExtractName(header, len, &name, &nameLen))
    {
        sendPEReply(ci, MIDICI_PE_STATUS_BAD_REQ, nullptr, 0);               // 400
        return;
    }

    int bodyLen = -1;
    if (peNameIs(name, nameLen, "ResourceList"))
    {
        bodyLen = buildResourceListJSON(peJson_, (int)sizeof(peJson_));
    }
    else if (peNameIs(name, nameLen, "DeviceInfo"))
    {
        bodyLen = buildDeviceInfoJSON(peJson_, (int)sizeof(peJson_));
    }
    else if (peNameIs(name, nameLen, "ChannelList"))
    {
        bodyLen = buildChannelListJSON(peJson_, (int)sizeof(peJson_));
    }
    else
    {
        const UMP_PEResource *r = findPEResource(name, nameLen);
        if (r != nullptr)
        {
            if (r->fields != nullptr)   // declarative
            {
                bodyLen = serializeFields(r->fields, r->fieldCount, peJson_, (int)sizeof(peJson_));
            }
            else if (r->get != nullptr) // escape hatch
            {
                bodyLen = r->get(peJson_, (int)sizeof(peJson_), r->ctx);
            }
        }
    }

    if (bodyLen < 0)
    {
        sendPEReply(ci, MIDICI_PE_STATUS_RESOURCE_UNSUPPORTED, nullptr, 0);   // 404
        return;
    }
    sendPEReply(ci, PE_STATUS_OK, (const uint8_t *)peJson_, (uint16_t)bodyLen);
}

// PE Set Inquiry: reassemble the body across the CI processor's <=256-byte
// slices, then on the final slice resolve the resource and apply. Field-based
// writable resources are applied via the field table (+ optional onApplied hook);
// escape-hatch resources go to their set() callback. Read-only -> 405, unknown ->
// 404, malformed header -> 400.
void UMP_Endpoint::onCIPESetInquiry(const MIDICI &ci, const char *header, uint16_t headerLen,
                                    const uint8_t *body, uint16_t bodyLen, bool lastByteOfSet)
{
    for (uint16_t i = 0; i < bodyLen; i++)
    {
        if (peSetLen_ < sizeof(peSetBuf_)) peSetBuf_[peSetLen_++] = body[i];
    }
    if (!lastByteOfSet)
    {
        return;   // more slices coming
    }

    uint16_t total = peSetLen_;
    peSetLen_ = 0;                        // reset for the next Set

    const char *name    = nullptr;
    uint16_t    nameLen = 0;
    if (!peExtractName(header, headerLen, &name, &nameLen))
    {
        sendPESetReplyStatus(ci, MIDICI_PE_STATUS_BAD_REQ);                   // 400
        return;
    }

    const UMP_PEResource *r = findPEResource(name, nameLen);
    if (r == nullptr)
    {
        sendPESetReplyStatus(ci, MIDICI_PE_STATUS_RESOURCE_UNSUPPORTED);      // 404
        return;
    }

    uint16_t status;
    if (r->fields != nullptr)
    {
        if (!r->writable)
        {
            status = MIDICI_PE_STATUS_RESOURCE_NOT_ALLOWED;                   // 405
        }
        else
        {
            applySetFields(*r, peSetBuf_, total);
            bool ok = (r->onApplied == nullptr) || r->onApplied(r->ctx);
            status = ok ? PE_STATUS_OK : MIDICI_PE_STATUS_BAD_REQ;           // 200 / 400
        }
    }
    else if (r->set != nullptr)
    {
        status = (uint16_t)r->set((const char *)peSetBuf_, total, r->ctx);
    }
    else
    {
        status = MIDICI_PE_STATUS_RESOURCE_NOT_ALLOWED;                       // 405
    }

    sendPESetReplyStatus(ci, status);
}

// Build a PE Get Reply: header = {"status":<code>}, body = JSON. Splits the body
// across multiple PE chunks when it won't fit the initiator's max SysEx in one
// message (the header rides chunk 1 only). Note: all chunks are queued into the
// outbound accumulator before the next flush, so a single reply is bounded by the
// TX buffer — ample for the resources here; a multi-KB property would need
// incremental flushing.
void UMP_Endpoint::sendPEReply(const MIDICI &ci, uint16_t status, const uint8_t *body, uint16_t bodyLen)
{
    char hdr[24];
    int  hlen = snprintf(hdr, sizeof(hdr), "{\"status\":%u}", (unsigned)status);
    if (hlen < 0 || hlen >= (int)sizeof(hdr))
    {
        return;
    }
    const uint8_t *b = (body != nullptr) ? body : (const uint8_t *)"";

    // Body room per chunk: chunk 1 also carries the header; chunks 2..N do not.
    uint16_t cap   = (peerMaxSysex_ < PE_OUT_MSG_CAP) ? peerMaxSysex_ : PE_OUT_MSG_CAP;
    int      room1 = (int)cap - PE_MSG_FIXED - hlen;
    int      roomN = (int)cap - PE_MSG_FIXED;
    if (room1 < 1) room1 = 1;
    if (roomN < 1) roomN = 1;

    uint16_t numChunks = 1;
    if ((int)bodyLen > room1)
    {
        numChunks = (uint16_t)(1 + ((int)bodyLen - room1 + roomN - 1) / roomN);
    }

    uint16_t off = 0;
    for (uint16_t chunk = 1; chunk <= numChunks; chunk++)
    {
        bool     first   = (chunk == 1);
        int      room    = first ? room1 : roomN;
        uint16_t thisLen = ((int)(bodyLen - off) < room) ? (uint16_t)(bodyLen - off) : (uint16_t)room;
        uint16_t n = CIMessage::sendPEGetReply(
            peSysex_, ci.ciVer, localMUID_, ci.remoteMUID, ci.requestId,
            first ? (uint16_t)hlen : 0, (uint8_t *)hdr,
            numChunks, chunk, thisLen, (uint8_t *)(b + off));
        sendCISysex(ci.umpGroup, peSysex_, n);
        off += thisLen;
    }
}

// PE Set Reply: status-only header, no body.
void UMP_Endpoint::sendPESetReplyStatus(const MIDICI &ci, uint16_t status)
{
    char hdr[24];
    int  hlen = snprintf(hdr, sizeof(hdr), "{\"status\":%u}", (unsigned)status);
    if (hlen < 0 || hlen >= (int)sizeof(hdr))
    {
        return;
    }
    uint16_t n = CIMessage::sendPESetReply(peSysex_, ci.ciVer, localMUID_, ci.remoteMUID,
                                           ci.requestId, (uint16_t)hlen, (uint8_t *)hdr);
    sendCISysex(ci.umpGroup, peSysex_, n);
}

// Foundational resource: DeviceInfo (M2-105). Numeric ids come from device
// metadata; human names from the endpoint/manufacturer strings; version from the
// same 4 bytes as the UMP Device Identity — no magic numbers.
int UMP_Endpoint::buildDeviceInfoJSON(char *out, int cap)
{
    int n = snprintf(out, (size_t)cap,
        "{\"manufacturerId\":[%u,%u,%u],"
        "\"familyId\":[%u,%u],"
        "\"modelId\":[%u,%u],"
        "\"versionId\":[%u,%u,%u,%u],"
        "\"manufacturer\":\"%s\","
        // M2-105 DeviceInfo requires a human-readable "family" name (minLength 1),
        // distinct from familyId. Placeholder "0,0" until a product family name is
        // assigned (familyId is currently 0,0).
        "\"family\":\"0,0\","
        "\"model\":\"%s\","
        "\"version\":\"%u.%u.%u.%u\"}",
        kmi_id_1, kmi_id_2, kmi_id_3,
        kmi_family_lsb, kmi_family_msb,
        getMidiProductId(), PID_MIDI_MSB,
        SYX_ID_APP_VER1, SYX_ID_APP_VER2, SYX_ID_APP_VER3, SYX_ID_APP_VER4,
        manufacturerName_, endpointName_,
        SYX_ID_APP_VER1, SYX_ID_APP_VER2, SYX_ID_APP_VER3, SYX_ID_APP_VER4);
    return (n < 0 || n >= cap) ? -1 : n;
}

// Foundational resource: ChannelList (M2-105). One playable channel (kick);
// channel numbers are 1-based. Title reuses the endpoint/instrument name.
int UMP_Endpoint::buildChannelListJSON(char *out, int cap)
{
    int n = snprintf(out, (size_t)cap, "[{\"title\":\"%s\",\"channel\":1}]", endpointName_);
    return (n < 0 || n >= cap) ? -1 : n;
}

// Foundational resource: ResourceList (M2-105) — every resource this device
// serves: the foundational ones plus each registered product resource, with
// canSet + an inline JSON Schema (explicit, or auto-derived from a field table).
int UMP_Endpoint::buildResourceListJSON(char *out, int cap)
{
    int n = snprintf(out, (size_t)cap, "[{\"resource\":\"DeviceInfo\"},{\"resource\":\"ChannelList\"}");
    if (n < 0 || n >= cap) return -1;

    for (uint8_t i = 0; i < peResCount_; i++)
    {
        const UMP_PEResource &r = peRes_[i];
        int m = snprintf(out + n, (size_t)(cap - n), ",{\"resource\":\"%s\"", r.name);
        if (m < 0 || m >= cap - n) return -1;
        n += m;

        bool settable = (r.fields != nullptr) ? r.writable : (r.set != nullptr);
        if (settable)
        {
            m = snprintf(out + n, (size_t)(cap - n), ",\"canSet\":\"full\"");
            if (m < 0 || m >= cap - n) return -1;
            n += m;
        }

        // Manufacturer-specific ("X-") resources must carry an inline JSON Schema:
        // an explicit one if provided, else derived from the declarative fields.
        if (r.schema != nullptr)
        {
            m = snprintf(out + n, (size_t)(cap - n), ",\"schema\":%s", r.schema);
            if (m < 0 || m >= cap - n) return -1;
            n += m;
        }
        else if (r.fields != nullptr)
        {
            m = snprintf(out + n, (size_t)(cap - n), ",\"schema\":");
            if (m < 0 || m >= cap - n) return -1;
            n += m;
            m = buildSchemaFromFields(r.title, r.fields, r.fieldCount, out + n, cap - n);
            if (m < 0) return -1;
            n += m;
        }

        if (n + 1 > cap) return -1;
        out[n++] = '}';
    }

    if (n + 2 > cap) return -1;
    out[n++] = ']';
    out[n]   = '\0';
    return n;
}

#endif // ENABLE_MIDI2
