/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Fel-Tracker)
 * Part of Fel-Tracker, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The plugin as DPF builds it (plugin/dpf: VST3, AU, CLAP): an instrument, MIDI in, the host's transport, its window (FelTrackerUI.cpp), 18 outputs: the
 * main pair and a pair a track (a bus each; AU: one bus of 18, the main its first pair). */
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND "Fel-Tracker"
#define DISTRHO_PLUGIN_NAME "Fel-Tracker"
#define DISTRHO_PLUGIN_URI "https://github.com/TristanoSuriani/fel-tracker"
#define DISTRHO_PLUGIN_CLAP_ID "org.fel-tracker.fel-tracker"

#define DISTRHO_PLUGIN_BRAND_ID FelT
#define DISTRHO_PLUGIN_UNIQUE_ID FeTr

#define DISTRHO_PLUGIN_HAS_UI 1
#define DISTRHO_UI_USE_NANOVG 1
#define DISTRHO_UI_USER_RESIZABLE 1
#define DISTRHO_UI_DEFAULT_WIDTH 1000
#define DISTRHO_UI_DEFAULT_HEIGHT 600
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_PLUGIN_IS_RT_SAFE 1
#define DISTRHO_PLUGIN_IS_SYNTH 1
#define DISTRHO_PLUGIN_NUM_INPUTS 0
#define DISTRHO_PLUGIN_NUM_OUTPUTS 18
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_LATENCY 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS 1
#define DISTRHO_PLUGIN_WANT_STATE 1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth"
#define DISTRHO_PLUGIN_CLAP_FEATURES "instrument", "synthesizer", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE aumu

#endif
