#pragma once
#include "gfx.h"
#include "subspace.h"
#include "weather.h"

class App;

void drawRedAlert(Gfx& g, const WeatherAlert& a, double t);
void drawIdle(App& app, Gfx& g, double t);
void drawTransmission(Gfx& g, const SubspaceMessage& m, double t);
