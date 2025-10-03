#pragma once
#ifdef _WIN32
// Trim Windows headers and stop min/max macros
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Winsock must come BEFORE windows.h
#include <winsock2.h>
#include <ws2tcpip.h>

// Some third-party headers pull in windows.h anyway — include it AFTER winsock2.h
#include <windows.h>

// Kill problematic macros (break Qt/moc and C++)
#ifdef interface
#undef interface
#endif
#ifdef ERROR
#undef ERROR
#endif
#endif
