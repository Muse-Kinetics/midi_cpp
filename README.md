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

### Products that use this library ###

* EM Pro EMC Control PCB
* EM Pro SoundStation
* EM Pro Sound Card

### Authors ###

* Eric Bateman - eric@musekinetics.com

### Licensing ###

Copyright (c) 2026 KMI Music, Inc.

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.