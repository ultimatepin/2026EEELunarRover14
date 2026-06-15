#pragma once

/*
  WebRoutes.h
  ===========

  This file exposes one function to the main .ino loop.

  Django analogy:
    WebRoutes.cpp contains both a tiny URL router and view functions.
    handleHttpClient() is called repeatedly from loop() and handles at most one
    pending browser/client connection each time.
*/

void handleHttpClient();
