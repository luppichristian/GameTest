/*
MIT License

Copyright (c) 2026 Christian Luppi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "Base.h"

// Redirect file access from the original path to a new path.
// This is used by the input recorder/player to transparently redirect the game's file access to the recorded input files,
// without requiring any changes to the game code.
GAME_TEST_API bool GameTest_FileManager_Redirect(const char* orignal_path, const char* new_path);

// Clear all file redirections set up by Redirect().
// This is called automatically when the file manager is quit, but can be called manually if needed (e.g. to clear a redirection before quitting).
GAME_TEST_API bool GameTest_FileManager_ClearRedirects(void);

// Set the working directory for relative paths.
// This is used by the input recorder/player to set the working directory to the location of the recorded input files, so that relative paths in the game code will correctly resolve to the files in that directory.
GAME_TEST_API bool GameTest_FileManager_SetWorkingDirectory(const char* path);

// Get the current working directory.
GAME_TEST_API bool GameTest_FileManager_GetWorkingDirectory(char* buffer, size_t buffer_size);

// Find a file by searching in the current working directory, and paths specified in the enviroment variables.
GAME_TEST_API bool GameTest_FileManager_Find(const char* filename, char* buffer, size_t buffer_size);

// Add a path to the list of paths that will be searched by Find().
GAME_TEST_API bool GameTest_FileManager_AddFindPath(const char* path);

// Clear all paths added by AddFindPath() and reset the search paths to just the current working directory.
GAME_TEST_API bool GameTest_FileManager_ClearFindPaths(void);

// File management (e.g. for input recording/playback).
GAME_TEST_API bool GameTest_FileManager_Init(void);

// Quit the file manager and release any resources. This should be called at the end of the program, after all file operations are done.
GAME_TEST_API void GameTest_FileManager_Quit(void);