#pragma once
#include "model.h"

namespace storage {

enum class Result : uint8_t { Ok, NoSd, NoMemory, NotFound, WriteFail, ReadFail, BadCrc, BadVersion, BadFile, EngineBusy };

const char* resultText(Result r);  // short, upper case, for toasts

// Keeps [A-Za-z0-9_-], at most 16 chars. False when nothing is left.
bool sanitize(const char* in, char out[17]);
// True when name survives sanitize() unchanged (so a listed file loads under its own name).
bool validName(const char* name);

bool exists(const char* name, bool bak = false);  // /projects/name.mtp (.bak)

// Posts Stop and waits until the engine is neither playing nor paused. False: not stopped after 200 ms.
bool stopEngine();

// Snapshot of live under the lock, .tmp, verify, rotate .bak, rename, /last.txt.
// Works while playing. On success live.name = name.
Result save(mt::Project& live, const char* name);

// Wi-Fi upload: checks tmpPath as a project (CRC, version) and moves it to /projects/fileName
// (<name>.mtp or <name>.bak). Replacing a .mtp rotates the old one to .bak, like save().
// On failure tmpPath is left for the caller to remove.
Result installProject(const char* tmpPath, const char* fileName);

// Reads into a temporary Project; on success stops the engine and replaces live.
// live.name = name (must be validName). EngineBusy: the engine did not stop, live untouched.
// The caller clears undo and the dirty flag.
Result load(mt::Project& live, const char* name, bool fromBak = false);

// Stops the engine, resets live and forgets /last.txt. EngineBusy: live untouched.
Result newProject(mt::Project& live);

// Boot: loads /last.txt straight into p (engine not running yet). False = p left reset.
// *fromBak = true when the .mtp was unreadable and the .bak was loaded instead.
// failed: set when the project named in /last.txt could not be loaded (demo is used instead).
bool autoload(mt::Project& p, bool* fromBak = nullptr, Result* failed = nullptr);

}  // namespace storage
