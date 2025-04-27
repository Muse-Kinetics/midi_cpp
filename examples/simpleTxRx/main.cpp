#include "simpleTxRx.hpp"

/*
  ----------------------------------------------------------------------------
  File:        main.cpp
  Description: Example application demonstrating how to use the MIDI_CPP library 
  for sending and receiving MIDI messages, including KMI/MK formatted SysEx messages.

  Copyright © 2025 KMI Music, Inc. All rights reserved.
  Unauthorized copying of this file, via any medium, is strictly prohibited.
  Proprietary and confidential.

  ----------------------------------------------------------------------------
*/

int main()
{
    SimpleTxRx example;
    example.setup();


    example.sendExampleMessages();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << std::endl << "TX/RX Demonstration Complete!" << std::endl;

}