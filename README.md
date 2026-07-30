# MIDI_CPP #

A MIDI library for Muse Kinetics and KMI products. Intended for 32bit embedded and desktop applications, this library should limit itself to the c++11 std library while maintaining a small memory footprint. 

This library should remain compatible with the KMI MIDI Device Manage (KMDM) library that uses the Qt framework, but it should not use any Qt methods or libraries. 

### Gotchas ###
* CRC checks are dependent on system architecture (8bit, 32bit, little/big endian). Legacy KMI devices that crc was implemented under were 8bit little-endian. If CRC checks fail there is a debug message you can set a breakpoint on to trace the issue, see utils_crc.h for more info

### Functionality ###

* Bytestream parsing and formatting
* SysEx Universal messagine, mainly ID requests/reply
* Device metadata
* KMI formatted sysex - preamble, 8<>7 bit encoding/decoding, CRC, message category / type
* USB MIDI 2.0 / UMP endpoint (optional, `ENABLE_MIDI2`) — see below

### MIDI 2.0 / UMP (optional) ###

`MIDI_ump.hpp` provides `UMP_Endpoint`, a transport- and product-agnostic USB
MIDI 2.0 / Universal MIDI Packet engine built on
[AM_MIDI2.0Lib](https://github.com/midi2-dev/AM_MIDI2.0Lib). It implements:

* UMP Stream Endpoint / Function Block Discovery
* MIDI 2.0 Channel Voice TX (MT 0x4), including a MIDI 1.0 → UMP bridge
* MIDI-CI Capability Inquiry: Discovery (random per-boot MUID), Profile
  Configuration, and Property Exchange
* Property Exchange foundational resources (ResourceList / DeviceInfo /
  ChannelList) plus a **declarative, product-registered resource table** with
  Get/Set — map JSON keys straight onto backing variables and the engine
  auto-serialises Get, auto-applies Set, and auto-derives the JSON Schema.
* MIDI-CI **Profile Configuration** via a **product-registered profile table**:
  the engine runs the generic Common-Rules envelope (Profile Inquiry/list, Set
  On/Off with Enabled/Disabled notifications, Profile Details Inquiry, and
  opaque Profile-Specific-Data routing) for any profile, while the profile's
  5-byte ID, address (channel / group / function block), and profile-specific
  messaging stay in the application. Profile Configuration is advertised in
  Discovery automatically when at least one profile is registered.

It is entirely compiled out unless `ENABLE_MIDI2` is defined, so MIDI 1.0
behaviour is unchanged when the flag is off. AM_MIDI2.0Lib is an **optional**
dependency: all of its includes are guarded by `ENABLE_MIDI2`.

**PlatformIO note:** consuming projects must set `lib_ldf_mode = chain+` so the
dependency finder honours the `#ifdef ENABLE_MIDI2` guards (plain `chain` mode
text-scans includes and would demand AM_MIDI2.0Lib even with the flag off).

The application supplies the transport, MCU serial/entropy, identity strings, and
its Property Exchange resource table, and (optionally) its MIDI-CI Profile table.
Copy-and-adapt templates (`.template`, mirroring the `MIDI_CPP_config.hpp.template`
pattern — copy into your app and drop the suffix) are provided for the application
glue: `include/midi2.hpp.template`, `src/midi2.cpp.template`,
`include/midi2_resources.hpp.template`, `src/midi2_resources.cpp.template`,
`include/midi2_profiles.hpp.template`, and `src/midi2_profiles.cpp.template` (the
last pair only if the product implements MIDI-CI Profiles).

### Products that use this library ###

* Pearl MalletStation EM Pro
* EM Pro Sound Card
* More in development

### Authors ###

* Eric Bateman - eric@musekinetics.com

### Licensing ###

Copyright (c) 2026 KMI Music, Inc.

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

The optional MIDI 2.0 / UMP engine builds on
[AM_MIDI2.0Lib](https://github.com/midi2-dev/AM_MIDI2.0Lib) by Andrew Mee, also
MIT licensed. It is a separate dependency and is only compiled when
`ENABLE_MIDI2` is defined.
