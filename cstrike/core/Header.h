#pragma once

// Placeholder binary assets to avoid large braced-init narrowing errors
// The original file contained large embedded binary arrays which produce
// C++ list-initialization narrowing diagnostics on some toolchains.
// These small placeholders preserve `sizeof()` semantics and can be
// replaced by the original assets later (or moved to a .cpp data file).

static const unsigned char Icons[] = { 0x00 };
static const unsigned int Icons_size = sizeof(Icons);

static const unsigned char icon[]  = { 0x00 };
static const unsigned int icon_size = sizeof(icon);

static const unsigned char logo[]  = { 0x00 };
static const unsigned int logo_size = sizeof(logo);
