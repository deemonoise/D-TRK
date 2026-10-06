#pragma once
#include <stdint.h>
#include "model.h"

namespace storage {

enum class Result : uint8_t { Ok, NoSd, NoMemory, NotFound, WriteFail, ReadFail, BadCrc, BadVersion, BadFile, EngineBusy,
                            SamplesNotSaved };

const char* resultText(Result r);  // short, upper case, for toasts

// Keeps [A-Za-z0-9_-], at most 16 chars. False when nothing is left.
bool sanitize(const char* in, char out[17]);
// True when name survives sanitize() unchanged (so a listed file loads under its own name).
bool validName(const char* name);

bool exists(const char* name, bool bak = false);  // /projects/name.mtp (.bak)

// Posts Stop and waits until the engine is neither playing nor paused. False: not stopped after 200 ms.
bool stopEngine();

// Progress of the sample sync: current sample name, bytes (or work units) of it.
using SyncProgress = void (*)(const char* file, uint32_t done, uint32_t total, void* ctx);

// Snapshot of live under the lock, .tmp, verify, rotate .bak, rename, /last.txt, then syncFolder().
// Works while playing. On success live.name = name. SamplesNotSaved: the .mtp is saved (live.name
// set) but the sample folder is not complete.
Result save(mt::Project& live, const char* name, SyncProgress cb = nullptr, void* ctx = nullptr);

// /projects/<live.name>/<sample>.wav for every listed sample that is cached and whose file is absent
// or has another crc ("mtcr"; a file without one is rewritten); other .wav files there are removed.
// Samples not cached (missing) keep their files; from (the folder the project came from, Save As): such
// a sample without its current file here gets a copy of /projects/<from>/<sample>.wav. Leftover .tmp
// files go too. Orphans are only removed when every write succeeded. Works while playing.
Result syncFolder(const mt::Project& live, const char* from = nullptr, SyncProgress cb = nullptr, void* ctx = nullptr);

// After load / autoload, engine stopped: an old file without a list is migrated first, then every
// listed sample not cached is imported from /projects/<live.name>/ (an old one also from the folder of
// the project that migrated it, /projects/legacy.idx). *missing = samples (or old names) left without
// data; they stay in the list and play silent. After a migration the folder is written right away:
// SamplesNotSaved if that failed.
Result pullSamples(mt::Project& live, int* missing, SyncProgress cb = nullptr, void* ctx = nullptr);

// Wi-Fi upload: checks tmpPath as a project (CRC, version) and moves it to /projects/fileName
// (<name>.mtp or <name>.bak). Replacing a .mtp rotates the old one to .bak, like save().
// On failure tmpPath is left for the caller to remove.
Result installProject(const char* tmpPath, const char* fileName);

// Reads into a temporary Project; on success stops the engine and replaces live.
// live.name = name (must be validName). EngineBusy: the engine did not stop, live untouched.
// Then pullSamples(): *missing (optional) = samples left without data. SamplesNotSaved: loaded, but
// the folder write after a migration failed.
// The caller clears undo and the dirty flag.
Result load(mt::Project& live, const char* name, bool fromBak = false, int* missing = nullptr,
            SyncProgress cb = nullptr, void* ctx = nullptr);

// Stops the engine, resets live and forgets /last.txt. EngineBusy: live untouched.
Result newProject(mt::Project& live);

// Boot: loads /last.txt straight into p (engine not running yet). False = p left reset.
// *fromBak = true when the .mtp was unreadable and the .bak was loaded instead.
// failed: set when the project named in /last.txt could not be loaded (demo is used instead).
bool autoload(mt::Project& p, bool* fromBak = nullptr, Result* failed = nullptr);

}  // namespace storage
