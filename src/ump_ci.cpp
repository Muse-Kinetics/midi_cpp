#ifdef UMP_HOST
/*
  ----------------------------------------------------------------------------
  File:        ump_ci.cpp
  Project:     UMP_io

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>
  ----------------------------------------------------------------------------

  MIDI-CI / UMP-Stream device-host implementation (see ump_ci.hpp). Rides
  ump::Transport for I/O and wraps AM_MIDI2.0Lib (umpProcessor + midiCIProcessor)
  for message create/parse. Validated headless against a real MIDI 2.0 device:
  UMP-Stream discovery, MIDI-CI discovery, PE capability + GET ResourceList.

  Notes baked in from that validation:
    * Use MIDI-CI v1.2 (ciVer = 2); v1 PE-capability replies trip AM's parser.
    * AM bug (midiCIProcessor.cpp:617): the PE-Capabilities-REPLY dispatch guards
      on the *inquiry* callback pointer, so we also set the inquiry callback to a
      no-op so the reply callback fires.
    * UMP-Stream numFunctionBlocks byte carries the static-FB flag in bit 7.
*/
#include "ump/ump_ci.hpp"

#include "umpProcessor.h"
#include "umpMessageCreate.h"
#include "midiCIProcessor.h"
#include "midiCIMessageCreate.h"

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace ump { namespace ci {

static constexpr uint8_t  kCIVer      = 2;      // MIDI-CI 1.2
static constexpr uint8_t  kDest       = 0x7F;   // whole function block / to device
static constexpr uint32_t kBroadcast  = 0x0FFFFFFF;

static uint32_t nextMuid()
{
    static std::atomic<uint32_t> c{0};
    uint32_t v = 0x0600000u + (c.fetch_add(1) & 0xFFFFu);
    return v & 0x0FFFFFFFu;
}

// Fragment a CI SysEx byte buffer into UMP SysEx7 (MT3) packets and send.
static void sendCISysex(OutputPort *out, const uint8_t *b, size_t total)
{
    if (!out || total == 0) return;
    bool single = (total <= 6);
    for (size_t off = 0; off < total; off += 6) {
        size_t ch = (total - off < 6) ? (total - off) : 6;
        uint8_t st = single ? 0 : (off == 0 ? 1 : (off + ch >= total ? 3 : 2));
        uint8_t sx[6] = {0,0,0,0,0,0};
        for (size_t i = 0; i < ch; i++) sx[i] = b[off + i] & 0x7F;
        uint32_t w0 = (0x3u << 28) | (0u << 24) | ((uint32_t)st << 20)
                    | ((uint32_t)ch << 16) | ((uint32_t)sx[0] << 8) | sx[1];
        uint32_t w1 = ((uint32_t)sx[2] << 24) | ((uint32_t)sx[3] << 16) | ((uint32_t)sx[4] << 8) | sx[5];
        uint32_t w[2] = { w0, w1 };
        out->send(w, 2);
    }
}

// ----------------------------------------------------------------------------

class SessionImpl : public Session
{
public:
    SessionImpl(Transport &t, std::string name)
        : transport_(t), name_(std::move(name)), localMuid_(nextMuid()) {}

    void open()
    {
        input_  = transport_.openInput(name_, [this](const uint32_t *w, size_t n) { onRx(w, n); });
        output_ = transport_.openOutput(name_);
        wire();
    }

    const std::string  &deviceName() const override { return name_; }
    SessionStatus       status()     const override { return status_.load(); }
    const EndpointInfo &endpoint()   const override { return ep_; }
    EndpointInfo         endpointCopy() override { std::lock_guard<std::recursive_mutex> lk(mtx_); return ep_; }
    std::vector<Profile> profilesCopy() override { std::lock_guard<std::recursive_mutex> lk(mtx_); return profiles_; }

    void discover() override
    {
        setStatus(SessionStatus::Discovering);
        // UMP-Stream: endpoint + all notifications, then all function blocks (info+name).
        auto ep = UMPMessage::mtFMidiEndpoint(0x1F);
        uint32_t e[4] = { ep[0], ep[1], ep[2], ep[3] }; sendUMP(e, 4);
        auto fb = UMPMessage::mtFFunctionBlock(0xFF, 0x03);
        uint32_t f[4] = { fb[0], fb[1], fb[2], fb[3] }; sendUMP(f, 4);
        // MIDI-CI discovery.
        uint8_t sx[64];
        uint16_t len = CIMessage::sendDiscoveryRequest(sx, kCIVer, localMuid_,
            {0,0,0}, {0,0}, {0,0}, {0,0,0,0}, 0x1E, 512, 0);
        sendCISysex(output_.get(), sx, len);
    }

    // ---- Property Exchange ----
    void resourceList(ResourcesCb cb) override
    {
        getProperty("ResourceList", "", [this, cb](bool ok, const std::string &json, const std::string &err) {
            std::vector<ResourceEntry> out;
            if (ok) parseResourceList(json, out);
            if (cb) cb(out);
        });
    }
    void getProperty(const std::string &resource, const std::string & /*path*/, JsonReplyCb cb) override
    {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        Pending p; p.cb = std::move(cb);
        std::string header = "{\"resource\":\"" + resource + "\"}";
        if (remoteMuid_ && peReady_) sendGet(header, p);
        else queuedGets_.push_back({ header, p });   // flush once PE is ready
    }
    void setProperty(const std::string &resource, const std::string & /*path*/,
                     const std::string &json, JsonReplyCb cb) override
    {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        if (!remoteMuid_ || !peReady_) { if (cb) cb(false, "", "session not ready"); return; }
        uint8_t sx[2048];
        std::string header = "{\"resource\":\"" + resource + "\"}";
        uint8_t reqId = nextReqId_++;
        uint16_t len = CIMessage::sendPESet(sx, kCIVer, localMuid_, remoteMuid_, reqId,
            (uint16_t)header.size(), (uint8_t *)header.data(), 1, 1,
            (uint16_t)json.size(), (uint8_t *)json.data());
        Pending p; p.cb = std::move(cb); pending_[reqId] = p;
        sendCISysex(output_.get(), sx, len);
    }
    int subscribe(const std::string &resource, SubscribeCb cb) override
    {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        int subId = nextSubId_++;
        Sub s; s.subId = subId; s.resource = resource; s.cb = std::move(cb);
        subs_[subId] = s;
        if (remoteMuid_ && peReady_) sendSubStart(subId, resource);
        else queuedSubs_.push_back(subId);   // flush once PE is ready
        return subId;
    }
    void unsubscribe(int subId) override
    {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        auto it = subs_.find(subId); if (it == subs_.end()) return;
        const std::string sid = it->second.subscribeId;
        if (remoteMuid_ && peReady_ && !sid.empty()) {
            uint8_t sx[256]; uint8_t reqId = nextReqId_++;
            std::string header = "{\"command\":\"end\",\"subscribeId\":\"" + sid + "\"}";
            uint16_t len = CIMessage::sendPESub(sx, kCIVer, localMuid_, remoteMuid_, reqId,
                (uint16_t)header.size(), (uint8_t *)header.data(), 1, 1, 0, nullptr);
            sendCISysex(output_.get(), sx, len);
        }
        if (!sid.empty()) bySubscribeId_.erase(sid);
        subs_.erase(it);
    }
    void subscribeProperty(const std::string &resource) override
    {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        for (auto &kv : subs_) if (kv.second.resource == resource) return;   // already subscribed
        subscribe(resource, nullptr);   // notifications also land in replies_ cache (poll)
    }
    void unsubscribeProperty(const std::string &resource) override
    {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        for (auto &kv : subs_) if (kv.second.resource == resource) { unsubscribe(kv.first); return; }
    }

    // ---- poll-friendly PE (Max clock reads these; no host callback) ----
    void requestProperty(const std::string &resource) override
    {
        getProperty(resource, "", [this, resource](bool ok, const std::string &json, const std::string &) {
            std::lock_guard<std::recursive_mutex> lk(mtx_);
            if (ok) { replies_[resource] = json; repliesDirty_.insert(resource); }
        });
    }
    bool takeProperty(const std::string &resource, std::string &out) override
    {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        auto it = repliesDirty_.find(resource);
        if (it == repliesDirty_.end()) return false;
        out = replies_[resource]; repliesDirty_.erase(it); return true;
    }
    void requestSet(const std::string &resource, const std::string &json) override
    {
        setProperty(resource, "", json, [](bool, const std::string &, const std::string &) {});
    }

    // ---- Profiles ----
    void inquireProfiles(ProfilesCb cb) override
    {
        { std::lock_guard<std::recursive_mutex> lk(mtx_); profilesCb_ = cb; profiles_.clear(); }
        sendProfileInquiryAll();
    }
    void enableProfile(const uint8_t id[5], uint8_t /*group*/, uint8_t channel, bool on) override
    {
        if (!remoteMuid_) return;
        std::array<uint8_t,5> pid{ id[0], id[1], id[2], id[3], id[4] };
        uint8_t sx[64];
        uint16_t len = on
            ? CIMessage::sendProfileOn(sx, kCIVer, localMuid_, remoteMuid_, channel, pid, 1)
            : CIMessage::sendProfileOff(sx, kCIVer, localMuid_, remoteMuid_, channel, pid);
        sendCISysex(output_.get(), sx, len);
    }

    // ---- events ----
    void onStatusChanged(StatusCb cb)   override { statusCbs_.push_back(cb); }
    void onEndpointChanged(EndpointCb cb) override { epCbs_.push_back(cb); }
    void onProfileChanged(ProfilesCb cb)  override { profileListeners_.push_back(cb); }

private:
    struct Pending { std::string resource; std::string body; JsonReplyCb cb; };
    struct Sub { int subId = 0; std::string resource; SubscribeCb cb; std::string subscribeId; };

    void onRx(const uint32_t *w, size_t n)
    {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        for (size_t i = 0; i < n; i++) ump_.processUMP(w[i]);
    }
    void sendUMP(const uint32_t *w, size_t n) { if (output_) output_->send(w, n); }

    void setStatus(SessionStatus s) { status_.store(s); for (auto &cb : statusCbs_) if (cb) cb(s); }
    void notifyEndpoint()           { for (auto &cb : epCbs_) if (cb) cb(ep_); }
    void notifyProfiles()           { if (profilesCb_) profilesCb_(profiles_); for (auto &cb : profileListeners_) if (cb) cb(profiles_); }

    void sendGet(const std::string &header, Pending p)   // caller holds mtx_
    {
        uint8_t sx[512];
        uint8_t reqId = nextReqId_++;
        uint16_t len = CIMessage::sendPEGet(sx, kCIVer, localMuid_, remoteMuid_, reqId,
            (uint16_t)header.size(), (uint8_t *)header.data());
        pending_[reqId] = p;
        sendCISysex(output_.get(), sx, len);
    }

    void sendSubStart(int subId, const std::string &resource)   // caller holds mtx_
    {
        uint8_t sx[512];
        uint8_t reqId = nextReqId_++;
        std::string header = "{\"resource\":\"" + resource + "\",\"command\":\"start\"}";
        uint16_t len = CIMessage::sendPESub(sx, kCIVer, localMuid_, remoteMuid_, reqId,
            (uint16_t)header.size(), (uint8_t *)header.data(), 1, 1, 0, nullptr);
        pendingSubReq_[reqId] = subId;
        sendCISysex(output_.get(), sx, len);
    }
    void sendSubReplyAck(uint8_t reqId)   // caller holds mtx_ — ack a device push
    {
        uint8_t sx[128];
        std::string header = "{\"status\":200}";
        uint16_t len = CIMessage::sendPESubReply(sx, kCIVer, localMuid_, remoteMuid_, reqId,
            (uint16_t)header.size(), (uint8_t *)header.data());
        sendCISysex(output_.get(), sx, len);
    }
    // Minimal string-value extractor for a top-level JSON key ("key":"value").
    static std::string jsonStr(const std::string &j, const std::string &key)
    {
        std::string pat = "\"" + key + "\"";
        size_t p = j.find(pat);          if (p == std::string::npos) return "";
        size_t c = j.find(':', p + pat.size()); if (c == std::string::npos) return "";
        size_t q1 = j.find('"', c + 1);  if (q1 == std::string::npos) return "";
        size_t q2 = j.find('"', q1 + 1); if (q2 == std::string::npos) return "";
        return j.substr(q1 + 1, q2 - q1 - 1);
    }

    void wire()
    {
        // ---- UMP Stream (device info + function blocks) ----
        ump_.setMidiEndpointInfoNotify([this](uint8_t maj, uint8_t min, uint8_t nfb, bool, bool, bool, bool) {
            std::lock_guard<std::recursive_mutex> lk(mtx_); ep_.umpVersionMajor = maj; ep_.umpVersionMinor = min;
            (void)nfb; notifyEndpoint(); });
        ump_.setMidiEndpointNameNotify([this](umpData m) {
            std::lock_guard<std::recursive_mutex> lk(mtx_); ep_.name.assign((char *)m.data, m.dataLength); notifyEndpoint(); });
        ump_.setMidiEndpointProdIdNotify([this](umpData m) {
            std::lock_guard<std::recursive_mutex> lk(mtx_); ep_.productInstanceId.assign((char *)m.data, m.dataLength); notifyEndpoint(); });
        ump_.setMidiEndpointDeviceInfoNotify([this](std::array<uint8_t,3> man, std::array<uint8_t,2> fam, std::array<uint8_t,2> mod, std::array<uint8_t,4> ver) {
            std::lock_guard<std::recursive_mutex> lk(mtx_);
            ep_.identity.manufacturer[0]=man[0]; ep_.identity.manufacturer[1]=man[1]; ep_.identity.manufacturer[2]=man[2];
            ep_.identity.family = (uint16_t)(fam[0] | (fam[1] << 7));
            ep_.identity.model  = (uint16_t)(mod[0] | (mod[1] << 7));
            ep_.identity.revision = (uint32_t)(ver[0] | (ver[1]<<8) | (ver[2]<<16) | (ver[3]<<24));
            notifyEndpoint(); });
        ump_.setFunctionBlockNotify([this](uint8_t idx, bool active, uint8_t dir, bool, bool, uint8_t fg, uint8_t gl, uint8_t, uint8_t, uint8_t) {
            std::lock_guard<std::recursive_mutex> lk(mtx_);
            FunctionBlock &b = fbAt(idx); b.index=idx; b.active=active; b.direction=dir; b.firstGroup=fg; b.numGroups=gl; notifyEndpoint(); });
        ump_.setFunctionBlockNameNotify([this](umpData m, uint8_t idx) {
            std::lock_guard<std::recursive_mutex> lk(mtx_); fbAt(idx).name.assign((char *)m.data, m.dataLength); notifyEndpoint(); });
        // no-ops for the rest (a live device streams these)
        ump_.setMidiEndpoint([](uint8_t,uint8_t,uint8_t){}); ump_.setFunctionBlock([](uint8_t,uint8_t){});
        ump_.setStreamConfigNotify([](uint8_t,bool,bool){}); ump_.setStreamConfigRequest([](uint8_t,bool,bool){});
        ump_.setStartOfSeq([](){}); ump_.setEndOfFile([](){}); ump_.setUnknownUMP([](uint32_t*,uint8_t){});
        ump_.setCVM([](umpCVM){}); ump_.setSystem([](umpGeneric){}); ump_.setUtility([](umpGeneric){});
        // ---- route SysEx7 -> MIDI-CI processor; also parse Profile replies ourselves ----
        // (AM's Profile-Inquiry-Reply parser misaligns the 5-byte IDs, so we reassemble
        //  the CI message and parse profiles directly.)
        ump_.setSysEx([this](umpData m) {
            if (m.form == 0 || m.form == 1) ciMsg_.clear();
            for (int i = 0; i < m.dataLength; i++) ciMsg_.push_back(m.data[i]);
            if (m.form == 0 || m.form == 3) {
                ci_.startSysex7(m.umpGroup, kDest);
                for (uint8_t b : ciMsg_) ci_.processMIDICI(b);
                ci_.endSysex7();
                parseCIMessage(ciMsg_);
            }
        });

        // ---- MIDI-CI ----
        ci_.setCheckMUID([this](uint8_t, uint32_t muid, void *) { return muid == localMuid_; });
        ci_.setRecvDiscoveryReply([this](MIDICI ci, std::array<uint8_t,3>, std::array<uint8_t,2>, std::array<uint8_t,2>, std::array<uint8_t,4>, uint8_t sup, uint16_t, uint8_t, uint8_t) {
            std::lock_guard<std::recursive_mutex> lk(mtx_);
            remoteMuid_ = ci.remoteMUID; ep_.muid = remoteMuid_;
            ep_.supportsProfiles = (sup >> 2) & 1; ep_.supportsPE = (sup >> 3) & 1;
            notifyEndpoint();
            if (ep_.supportsPE) {   // negotiate PE capabilities
                uint8_t sx[64]; uint16_t l = CIMessage::sendPECapabilityRequest(sx, kCIVer, localMuid_, remoteMuid_, 4, 0, 0);
                sendCISysex(output_.get(), sx, l);
            } else setStatus(SessionStatus::Ready);
            if (ep_.supportsProfiles) sendProfileInquiryAll();
        });
        ci_.setPECapabilities([](MIDICI,uint8_t,uint8_t,uint8_t){});   // AM bug workaround (see header)
        ci_.setPECapabilitiesReply([this](MIDICI, uint8_t, uint8_t, uint8_t) {
            std::lock_guard<std::recursive_mutex> lk(mtx_); peReady_ = true;
            for (auto &q : queuedGets_) sendGet(q.first, q.second); queuedGets_.clear();
            for (int subId : queuedSubs_) { auto s = subs_.find(subId); if (s != subs_.end()) sendSubStart(subId, s->second.resource); }
            queuedSubs_.clear();
            setStatus(SessionStatus::Ready);
        });
        ci_.setRecvPEGetReply([this](MIDICI ci, std::string, uint16_t bl, uint8_t *body, bool, bool lastSet) {
            std::lock_guard<std::recursive_mutex> lk(mtx_);
            auto it = pending_.find(ci.requestId); if (it == pending_.end()) return;
            it->second.body.append((char *)body, bl);
            if (lastSet) { auto cb = it->second.cb; std::string b = it->second.body; pending_.erase(it); if (cb) cb(true, b, ""); }
        });
        ci_.setRecvPESetReply([this](MIDICI ci, std::string) {
            std::lock_guard<std::recursive_mutex> lk(mtx_);
            auto it = pending_.find(ci.requestId); if (it == pending_.end()) return;
            auto cb = it->second.cb; pending_.erase(it); if (cb) cb(true, "", "");
        });
        // Subscribe reply (0x39): device assigns a subscribeId — bind it to our sub.
        ci_.setRecvPESubReply([this](MIDICI ci, std::string header) {
            std::lock_guard<std::recursive_mutex> lk(mtx_);
            auto it = pendingSubReq_.find(ci.requestId); if (it == pendingSubReq_.end()) return;
            int subId = it->second; pendingSubReq_.erase(it);
            auto s = subs_.find(subId); if (s == subs_.end()) return;
            std::string sid = jsonStr(header, "subscribeId");
            if (!sid.empty()) { s->second.subscribeId = sid; bySubscribeId_[sid] = subId; }
        });
        // Subscription push (0x38): device sends changed data. Reassemble chunks,
        // cache for the poll API, fire any callback, and ack.
        ci_.setRecvPESubInquiry([this](MIDICI ci, std::string header, uint16_t bl, uint8_t *body, bool, bool lastSet) {
            std::lock_guard<std::recursive_mutex> lk(mtx_);
            std::string &buf = subNotifyBuf_[ci.requestId];
            buf.append((char *)body, bl);
            if (!lastSet) return;
            std::string data = buf; subNotifyBuf_.erase(ci.requestId);
            std::string sid = jsonStr(header, "subscribeId");
            std::string cmd = jsonStr(header, "command");
            sendSubReplyAck(ci.requestId);
            auto b = bySubscribeId_.find(sid); if (b == bySubscribeId_.end()) return;
            auto s = subs_.find(b->second); if (s == subs_.end()) return;
            const std::string res = s->second.resource;
            if (cmd == "end") {   // device terminated the subscription
                SubscribeCb cb = s->second.cb; bySubscribeId_.erase(b); subs_.erase(s);
                if (cb) cb(res, "");
                return;
            }
            replies_[res] = data; repliesDirty_.insert(res);   // poll API sees it
            if (s->second.cb) s->second.cb(res, data);
        });
        ci_.setRecvProfileEnabled([](MIDICI, std::array<uint8_t,5>, uint8_t) {});   // parsed ourselves
        ci_.setRecvProfileDisabled([](MIDICI, std::array<uint8_t,5>, uint8_t) {});
    }

    FunctionBlock &fbAt(uint8_t idx)
    {
        for (auto &b : ep_.functionBlocks) if (b.index == idx) return b;
        ep_.functionBlocks.push_back(FunctionBlock{}); ep_.functionBlocks.back().index = idx;
        return ep_.functionBlocks.back();
    }
    // Profiles are addressed per channel/group/FB; inquire all common destinations
    // (this device reports profiles per channel, e.g. 0x00, not at the FB 0x7F).
    void sendProfileInquiryAll()
    {
        if (!remoteMuid_ || !output_) return;
        static const uint8_t dests[] = { 0x7F, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };
        for (uint8_t d : dests) { uint8_t sx[64]; uint16_t l = CIMessage::sendProfileListRequest(sx, kCIVer, localMuid_, remoteMuid_, d); sendCISysex(output_.get(), sx, l); }
    }
    void mergeProfile(const uint8_t *id, bool en, uint8_t channel)   // caller holds mtx_
    {
        for (auto &pr : profiles_) if (std::memcmp(pr.id, id, 5) == 0 && pr.channel == channel) { pr.enabled = en; return; }
        Profile pr; std::memcpy(pr.id, id, 5); pr.enabled = en; pr.channel = channel; profiles_.push_back(pr);
    }
    // Parse MIDI-CI Profile replies ourselves. Header: 7E <dest> 0D <subId> <ver>
    // <srcMUID:4> <destMUID:4> => data at [13]. Inquiry Reply (0x21):
    // <enCount:2><en*5><disCount:2><dis*5>. Enabled/Disabled report (0x24/0x25): <id:5>.
    void parseCIMessage(const std::vector<uint8_t> &b)
    {
        if (b.size() < 14 || b[0] != 0x7E || b[2] != 0x0D) return;
        uint8_t subId = b[3], dest = b[1];
        if (subId == 0x21) {
            size_t p = 13; if (p + 2 > b.size()) return;
            int en = b[p] | (b[p + 1] << 7); p += 2;
            std::lock_guard<std::recursive_mutex> lk(mtx_);
            for (int i = 0; i < en && p + 5 <= b.size(); i++) { mergeProfile(&b[p], true, dest); p += 5; }
            if (p + 2 <= b.size()) { int dis = b[p] | (b[p + 1] << 7); p += 2;
                for (int i = 0; i < dis && p + 5 <= b.size(); i++) { mergeProfile(&b[p], false, dest); p += 5; } }
            notifyProfiles();
        } else if ((subId == 0x24 || subId == 0x25) && b.size() >= 18) {
            std::lock_guard<std::recursive_mutex> lk(mtx_); mergeProfile(&b[13], subId == 0x24, dest); notifyProfiles();
        }
    }
    static void parseResourceList(const std::string &json, std::vector<ResourceEntry> &out)
    {
        // Minimal scan for {"resource":"NAME"...} entries + canSet + inline schema.
        size_t pos = 0;
        while ((pos = json.find("\"resource\"", pos)) != std::string::npos) {
            size_t c = json.find(':', pos); size_t q1 = json.find('"', c + 1); size_t q2 = json.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) break;
            ResourceEntry e; e.resource = json.substr(q1 + 1, q2 - q1 - 1);
            size_t objEnd = json.find('}', q2);
            std::string chunk = json.substr(q2, (objEnd == std::string::npos ? json.size() : objEnd) - q2);
            e.canSet = chunk.find("\"canSet\"") != std::string::npos;
            out.push_back(e); pos = q2 + 1;
        }
    }

    Transport &transport_;
    std::string name_;
    uint32_t localMuid_, remoteMuid_ = 0;
    std::unique_ptr<InputPort>  input_;
    std::unique_ptr<OutputPort> output_;
    umpProcessor    ump_;
    midiCIProcessor ci_;
    EndpointInfo ep_;
    std::atomic<SessionStatus> status_{ SessionStatus::Idle };
    bool peReady_ = false;
    std::recursive_mutex mtx_;
    uint8_t nextReqId_ = 1;
    std::map<uint8_t, Pending> pending_;
    std::vector<std::pair<std::string, Pending>> queuedGets_;
    std::map<std::string, std::string> replies_;
    std::set<std::string> repliesDirty_;
    int nextSubId_ = 1;
    std::map<int, Sub> subs_;                     // subId -> subscription
    std::map<uint8_t, int> pendingSubReq_;        // our reqId -> subId (awaiting SubReply)
    std::map<std::string, int> bySubscribeId_;    // device subscribeId -> subId
    std::map<uint8_t, std::string> subNotifyBuf_; // device reqId -> notification reassembly
    std::vector<int> queuedSubs_;                 // subIds queued until PE ready
    std::vector<uint8_t> ciMsg_;
    std::vector<Profile> profiles_;
    ProfilesCb profilesCb_;
    std::vector<StatusCb>   statusCbs_;
    std::vector<EndpointCb> epCbs_;
    std::vector<ProfilesCb> profileListeners_;
};

// ----------------------------------------------------------------------------

class HostImpl : public Host
{
public:
    HostImpl(Transport &t, std::string name) : transport_(t), hostName_(std::move(name)) {}

    std::shared_ptr<Session> open(const std::string &endpointName) override
    {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        auto it = sessions_.find(endpointName);
        if (it != sessions_.end()) return it->second;
        auto s = std::make_shared<SessionImpl>(transport_, endpointName);
        sessions_[endpointName] = s;
        s->open();
        s->discover();
        return s;
    }
    std::shared_ptr<Session> find(const std::string &endpointName) override
    {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        auto it = sessions_.find(endpointName);
        return it == sessions_.end() ? nullptr : it->second;
    }
    std::vector<std::string> ciCapableEndpoints() override
    {
        std::vector<std::string> out;
        for (auto &e : transport_.endpoints(Direction::Source, Protocol::Midi2)) out.push_back(e.name);
        return out;
    }

private:
    Transport &transport_;
    std::string hostName_;
    std::recursive_mutex mtx_;
    std::map<std::string, std::shared_ptr<SessionImpl>> sessions_;
};

std::unique_ptr<Host> createHost(Transport &transport, const std::string &hostName)
{
    return std::unique_ptr<Host>(new HostImpl(transport, hostName));
}

Host &defaultHost()
{
    static std::unique_ptr<Transport> t = createTransport("Max UMP_io CI");
    static std::unique_ptr<Host> h = createHost(*t, "Max UMP_io");
    return *h;
}

}} // namespace ump::ci

#endif // UMP_HOST
