#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "core_ram.h"
#include "esp_heap_caps.h"
#include "sequencer.h"
#include "synth.h"

#define CHECK(expression)                                                                  \
  do {                                                                                     \
    if (!(expression))                                                                     \
      throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) +       \
                               ": " #expression);                                          \
  } while (0)

namespace fixture {
struct Block {
  size_t bytes;
  uint32_t caps;
};
std::unordered_map<void*, Block> blocks;
size_t internalUsed = 0;
size_t psramUsed = 0;
size_t internalLimit = 4 * 1024 * 1024;
size_t psramLimit = 4 * 1024 * 1024;
bool failNextInternal = false;
bool failNextPsram = false;
bool failNextPark = false;
unsigned synthMoves = 0;
unsigned seqMoves = 0;
unsigned frees = 0;
void* synth = nullptr;
void* seq = nullptr;

size_t allocationSize(void* pointer) {
  const auto found = blocks.find(pointer);
  CHECK(found != blocks.end());
  return found->second.bytes;
}

bool isInternal(void* pointer) {
  // Like esp_ptr_internal, classify interior pointers too: the old paired policy
  // deliberately handed an interior pointer to the engine.
  const auto address = reinterpret_cast<uintptr_t>(pointer);
  for (const auto& entry : blocks) {
    const auto base = reinterpret_cast<uintptr_t>(entry.first);
    if (address >= base && address - base < entry.second.bytes)
      return (entry.second.caps & MALLOC_CAP_INTERNAL) != 0;
  }
  CHECK(false);
  return false;
}

std::vector<unsigned char> bytesOf(void* pointer, size_t bytes) {
  CHECK(pointer);
  const auto* begin = static_cast<const unsigned char*>(pointer);
  return {begin, begin + bytes};
}

void* allocate(size_t bytes, uint32_t caps) {
  const bool internal = (caps & MALLOC_CAP_INTERNAL) != 0;
  CHECK(internal != ((caps & MALLOC_CAP_SPIRAM) != 0));
  bool& fail = internal ? failNextInternal : failNextPsram;
  if (fail) {
    fail = false;
    return nullptr;
  }
  size_t& used = internal ? internalUsed : psramUsed;
  const size_t limit = internal ? internalLimit : psramLimit;
  if (used > limit || bytes > limit - used) return nullptr;
  void* memory = std::malloc(bytes);
  CHECK(memory);
  blocks.emplace(memory, Block{bytes, caps});
  used += bytes;
  return memory;
}

void release(void* pointer) {
  if (!pointer) return;
  const auto found = blocks.find(pointer);
  // Reject interior pointers, double frees and frees of memory this owner never allocated.
  CHECK(found != blocks.end());
  size_t& used = (found->second.caps & MALLOC_CAP_INTERNAL) ? internalUsed : psramUsed;
  used -= found->second.bytes;
  blocks.erase(found);
  ++frees;
  std::free(pointer);
}

void releaseAll() {
  while (!blocks.empty()) release(blocks.begin()->first);
}

void* boot() {
  core_ram::reserve();
  CHECK(synth);
  CHECK(seq);
  CHECK(synth != seq);
  CHECK(!isInternal(seq));
  CHECK(allocationSize(seq) == sizeof(mt::Sequencer));
  // The boundary model treats objects as opaque byte payloads. Actual class
  // declarations supply their sizes; hardware tests separately use their constructors.
  std::memset(synth, 0x5a, sizeof(mt::Synth));
  std::memset(seq, 0xa6, sizeof(mt::Sequencer));
  return seq;
}

struct Snapshot {
  void* const seqPointer = seq;
  const std::vector<unsigned char> synthBytes = bytesOf(synth, sizeof(mt::Synth));
  const std::vector<unsigned char> seqBytes = bytesOf(seq, sizeof(mt::Sequencer));

  void check() const {
    CHECK(seq == seqPointer);
    CHECK(bytesOf(seq, sizeof(mt::Sequencer)) == seqBytes);
    CHECK(bytesOf(synth, sizeof(mt::Synth)) == synthBytes);
    CHECK(seqMoves == 0);
    CHECK(blocks.size() == 2);
  }
};
}  // namespace fixture

void* heap_caps_malloc(size_t bytes, uint32_t caps) { return fixture::allocate(bytes, caps); }
void heap_caps_free(void* pointer) { fixture::release(pointer); }

namespace audio {
void reserve(void* memory) { fixture::synth = memory; }
void* moveSynth(void* destination) {
  using namespace fixture;
  ++synthMoves;
  if (failNextPark) {
    failNextPark = false;
    return nullptr;
  }
  CHECK(destination && synth);
  void* previous = synth;
  std::memcpy(destination, previous, sizeof(mt::Synth));
  synth = destination;
  return previous;
}
}  // namespace audio

namespace engine {
void reserve(void* memory) { fixture::seq = memory; }
void* moveSeq(void* destination) {
  using namespace fixture;
  ++seqMoves;
  CHECK(destination && seq);
  void* previous = seq;
  std::memcpy(destination, previous, sizeof(mt::Sequencer));
  seq = destination;
  return previous;
}
}  // namespace engine

namespace {
using namespace fixture;

void bootPrioritizesSynth() {
  // The internal budget fits Synth alone, but cannot fit the old paired block.
  internalLimit = sizeof(mt::Synth);
  boot();
  CHECK(core_ram::internal());
  CHECK(isInternal(synth));
  CHECK(internalUsed == sizeof(mt::Synth));
  CHECK(psramUsed == sizeof(mt::Sequencer));
  CHECK(blocks.size() == 2);
}

void bootFallbackCanRestore() {
  internalLimit = 0;
  boot();
  CHECK(!core_ram::internal());
  CHECK(!isInternal(synth));
  const Snapshot snapshot;
  internalLimit = sizeof(mt::Synth);
  CHECK(core_ram::toInternal());
  CHECK(core_ram::internal());
  CHECK(isInternal(synth));
  snapshot.check();
  CHECK(psramUsed == sizeof(mt::Sequencer));
}

void failedPsramAllocationPreservesSource() {
  boot();
  const Snapshot snapshot;
  void* const previous = synth;
  const unsigned initialFrees = frees;
  failNextPsram = true;
  CHECK(!core_ram::toPsram());
  CHECK(synth == previous);
  CHECK(frees == initialFrees);
  CHECK(core_ram::internal());
  snapshot.check();
}

void failedParkToPsramReleasesOnlyDestination() {
  boot();
  const Snapshot snapshot;
  void* const previous = synth;
  const unsigned initialFrees = frees;
  failNextPark = true;
  CHECK(!core_ram::toPsram());
  CHECK(synth == previous);
  CHECK(frees == initialFrees + 1);
  CHECK(core_ram::internal());
  snapshot.check();
}

void failedInternalAllocationCanRetry() {
  boot();
  const Snapshot snapshot;
  CHECK(core_ram::toPsram());
  void* const previous = synth;
  const unsigned initialFrees = frees;
  internalLimit = 0;
  CHECK(!core_ram::toInternal());
  CHECK(synth == previous);
  CHECK(!core_ram::internal());
  CHECK(frees == initialFrees);
  snapshot.check();
  internalLimit = sizeof(mt::Synth);
  CHECK(core_ram::toInternal());
  CHECK(core_ram::internal());
  snapshot.check();
}

void failedParkToInternalCanRetry() {
  boot();
  const Snapshot snapshot;
  CHECK(core_ram::toPsram());
  void* const previous = synth;
  const unsigned initialFrees = frees;
  failNextPark = true;
  CHECK(!core_ram::toInternal());
  CHECK(synth == previous);
  CHECK(!core_ram::internal());
  CHECK(frees == initialFrees + 1);
  snapshot.check();
  CHECK(core_ram::toInternal());
  snapshot.check();
}

void repeatedCyclesKeepSeqAndHeapStable() {
  boot();
  const Snapshot snapshot;
  const size_t initialInternal = internalUsed;
  const size_t initialPsram = psramUsed;
  for (int cycle = 0; cycle < 100; ++cycle) {
    CHECK(core_ram::toPsram());
    CHECK(!core_ram::internal());
    CHECK(!isInternal(synth));
    snapshot.check();
    CHECK(internalUsed == 0);
    CHECK(psramUsed == sizeof(mt::Synth) + sizeof(mt::Sequencer));
    CHECK(!core_ram::toPsram());  // already external: no second allocation or copy
    CHECK(core_ram::toInternal());
    CHECK(core_ram::internal());
    snapshot.check();
    CHECK(internalUsed == initialInternal && psramUsed == initialPsram);
    CHECK(!core_ram::toInternal());  // already internal
  }
  CHECK(synthMoves == 200);
}

void failedSeqAllocationDoesNotPoisonSynthOwner() {
  failNextPsram = true;
  core_ram::reserve();
  CHECK(synth && isInternal(synth));
  CHECK(seq == nullptr);
  CHECK(blocks.size() == 1);
  std::memset(synth, 0x45, sizeof(mt::Synth));
  CHECK(core_ram::toPsram());
  CHECK(seq == nullptr && seqMoves == 0);
  CHECK(core_ram::toInternal());
  CHECK(blocks.size() == 1);
}

void unreservedOwnerDoesNothing() {
  CHECK(!core_ram::toInternal());
  CHECK(!core_ram::toPsram());
  CHECK(blocks.empty());
}

void failedSynthAllocationsDoNotOwnUnknownFallback() {
  failNextInternal = true;
  failNextPsram = true;
  core_ram::reserve();
  CHECK(!synth);
  CHECK(seq && !isInternal(seq));
  CHECK(!core_ram::internal());
  CHECK(!core_ram::toInternal());
  CHECK(!core_ram::toPsram());
  CHECK(blocks.size() == 1);
}

void run(const std::string& name) {
  if (name == "boot") bootPrioritizesSynth();
  else if (name == "fallback") bootFallbackCanRestore();
  else if (name == "ps_alloc") failedPsramAllocationPreservesSource();
  else if (name == "park_ps") failedParkToPsramReleasesOnlyDestination();
  else if (name == "int_alloc") failedInternalAllocationCanRetry();
  else if (name == "park_int") failedParkToInternalCanRetry();
  else if (name == "cycles") repeatedCyclesKeepSeqAndHeapStable();
  else if (name == "seq_fail") failedSeqAllocationDoesNotPoisonSynthOwner();
  else if (name == "unreserved") unreservedOwnerDoesNothing();
  else if (name == "synth_fail") failedSynthAllocationsDoNotOwnUnknownFallback();
  else CHECK(false);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    CHECK(argc == 2);
    run(argv[1]);
    fixture::releaseAll();
    CHECK(fixture::internalUsed == 0 && fixture::psramUsed == 0);
    std::printf("PASS %s (host Synth%zu Seq%zu)\n", argv[1], sizeof(mt::Synth), sizeof(mt::Sequencer));
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL %s\n", error.what());
    fixture::releaseAll();
    return 1;
  }
}
