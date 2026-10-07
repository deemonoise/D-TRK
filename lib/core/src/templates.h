#pragma once
#include "model.h"

namespace mt {

// Built-in project templates for FILE -> New: instruments from the factory presets, tracks set up,
// patterns and chain empty. Template 0 is EMPTY (a reset project).
int templateCount();
const char* templateName(int i);       // up to 10 characters
void templateBuild(int i, Project& p);  // p.reset(), then the template; i out of range = EMPTY

// A user template from a project: the same project without notes (patterns cleared to their
// defaults, chain and scenes empty, song mode off), named "untitled".
void templateStrip(Project& p);

}  // namespace mt
