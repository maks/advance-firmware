/*
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 */
#ifndef VIEWUTILS_H
#define VIEWUTILS_H

class GUIWindow;
struct GUIPoint;
class ViewData;
struct GUITextProperties;

void DrawLabeledField(GUIWindow &w, GUIPoint position, char *buffer,
                      const GUITextProperties &props);

#endif
