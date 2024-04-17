#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "LogParserCommon.h"
#include "SharedGlobals.h"

BOOL CALLBACK FixFont(HWND hwnd, LPARAM italic = 0);
void FixChildFonts(HWND hwnd);

void HandleDragDropFileResults(const std::vector<std::string> &files);
bool HandleDragDropFileBusyCheck();

void PromptAndParseDataFromClipboard();
void MoveAndLoadLogs(LogCollection &&logs, bool merge);

void AddDnsLookupColumnForIpColumn(int dataCol);

void CopyTextToClipboard(const std::string &text);

bool RunLoadSaveDialog(bool isSave, OPENFILENAME &ofn, std::string &chosenPath);

void PushLockoutInteraction(HWND interactionOwner = (HWND)INVALID_HANDLE_VALUE);
void PopLockoutInteraction();
extern HWND currentInteractionOwner;
extern HWND activeMainWindow;

const WORD ACCEL_DEBUGWINDOW = 13001;
const WORD ACCEL_COPY = 13002;
const WORD ACCEL_CUT = 13003;
const WORD ACCEL_PASTE = 13004;
const WORD ACCEL_SELALL = 13005;
const WORD ACCEL_FINDNEXT = 13006;
const WORD ACCEL_FINDPREV = 13007;
