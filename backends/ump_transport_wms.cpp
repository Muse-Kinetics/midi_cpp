/*
  ----------------------------------------------------------------------------
  File:        ump_transport_wms.cpp
  Project:     UMP_io  (Windows backend — PLACEHOLDER)

  Copyright (c) 2026 KMI Music, Inc.
  SPDX-License-Identifier: MIT

  Author: Eric Bateman <eric@musekinetics.com>
  ----------------------------------------------------------------------------

  Windows MIDI Services (WMS) backend for ump::Transport. NOT YET IMPLEMENTED —
  this stub exists to (a) keep the cross-platform contract compiling on Windows
  and (b) record how the WMS C++/WinRT API maps onto the neutral interface, so
  the CoreMIDI backend and this one stay shaped alike.

  WMS SDK mapping (Windows.Devices.Midi2, same SDK the Muse-Kinetics rtmidi WMS
  fork already uses in sendsysex):

    createTransport(name)          -> MidiSession::Create(name)
    endpoints(dir, Midi2)          -> MidiEndpointDeviceInformation::FindAll(),
                                      filter to UMP/native endpoints; DisplayName
                                      -> EndpointInfo.name, EndpointDeviceId ->
                                      EndpointInfo.id (hashed to uint64)
    setEndpointsChangedCallback    -> MidiEndpointDeviceWatcher (Added/Removed/
                                      Updated events)
    openInput(name, onRx)          -> MidiSession::CreateEndpointConnection(id);
                                      MessageReceived event -> read the UMP words
                                      (IMidiUniversalPacket) -> RxCallback
    openOutput(name)               -> MidiSession::CreateEndpointConnection(id)
    OutputPort::send(words,count)  -> SendSingleMessageWordArray / word-list send

  Threading, like CoreMIDI, delivers on a WinRT/OS thread; the consumer marshals.
*/
#if defined(_WIN32)

#include "ump/ump_transport.hpp"

namespace ump
{

std::unique_ptr<Transport> createTransport(const std::string & /*clientName*/)
{
    // TODO: implement against the Windows MIDI Services SDK (see mapping above).
    return nullptr;
}

} // namespace ump

#endif // _WIN32
