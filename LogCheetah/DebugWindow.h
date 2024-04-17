#pragma once

#include <string>

//Adds a message to the debug window, if it's present
void GlobalDebugOutput(const std::string &s);

//Show the debug window to the user if it's not already present
void ShowDebugWindow();

//Called by the main window loop to push queued messages out to the window
void ProcessAllQueuedDebugMessages();
