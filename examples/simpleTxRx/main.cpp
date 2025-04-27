#include "simpleTxRx.hpp"

int main()
{
    SimpleTxRx example;
    example.setup();

    // Keep the program alive to receive MIDI
    std::cout << "Waiting for MIDI input. Press Ctrl+C to exit..." << std::endl;
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        example.sendExampleMessages();
    }
}