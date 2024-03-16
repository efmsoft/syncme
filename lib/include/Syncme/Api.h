#pragma once

#if defined(_WIN32) && !defined(_SINCME_STATIC_BUILD_)
#ifdef _SYNCME_DLL_BUILD_
#define SINCMELNK __declspec(dllexport)
#else
#define SINCMELNK __declspec(dllimport)
#endif
#else
#define SINCMELNK
#endif
