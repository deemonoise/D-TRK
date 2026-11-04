"""Build a pinned, project-local SdFat IRQ fix; never edit the installed framework.

PlatformIO's PRE middleware replaces one translation unit and gives its generated copy the
upstream directory for quoted includes. The MIT copyright/license prefix stays in that copy.
"""
import hashlib
import json
from pathlib import Path


FRAMEWORK_VERSION = "1.162.0"
SOURCE_NAME = "SdioTeensy.cpp"
SOURCE_RELATIVE = Path("libraries/SdFat/src/SdCard") / SOURCE_NAME
ORIGINAL_SHA256 = "09d469a55d6b6a42ce6376c8d7bd172259518e084b0adadf4af6733854f47f4a"
PATCHED_SHA256 = "9055e083c964a32a574c5b46268ede8f3e7d4478bbe1bd84d30e60d619dc95d4"


# The transformation below contains portions of SdFat; its original notice follows.
# Copyright (c) 2011-2021 Bill Greiman
# This file is part of the SdFat library for SD memory cards.
#
# MIT License
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

MARKER = 'DTRK SDIO IRQ ownership fix'


def _replace(source, old, new, count=1):
    if not old:
        raise ValueError('empty SDIO fix anchor')
    found = source.count(old)
    if found != count:
        raise ValueError(f'SDIO fix anchor count {found}, expected {count}: {old.splitlines()[0]!r}')
    return source.replace(old, new)


def apply_dma_fix(source):
    if MARKER in source:
        raise ValueError('SDIO source already patched; middleware must verify its known digest')
    source = _replace(source,
'''const uint32_t SDHC_IRQSTAT_ERROR =
               SDHC_IRQSTAT_DMAE | SDHC_IRQSTAT_CMD_ERROR |
               SDHC_IRQSTAT_DATA_ERROR;''',
'''const uint32_t SDHC_IRQSTAT_ERROR =
               SDHC_IRQSTAT_DMAE | SDHC_IRQSTAT_CMD_ERROR |
               SDHC_IRQSTAT_DATA_ERROR;

// DTRK SDIO IRQ ownership fix: command polling and data completion have disjoint W1C flags.
// A preempting audio interrupt may let data finish before command polling resumes.
const uint32_t SDHC_IRQSTAT_CMD_MASK = SDHC_IRQSTAT_CC | SDHC_IRQSTAT_CMD_ERROR;
const uint32_t SDHC_IRQSTAT_DATA_TERMINAL = SDHC_IRQSTAT_TC | SDHC_IRQSTAT_DMAE |
                                          SDHC_IRQSTAT_DATA_ERROR;
const uint32_t SDHC_IRQSTAT_DATA_MASK = SDHC_IRQSTAT_DATA_TERMINAL | SDHC_IRQSTAT_DINT;''')
    source = _replace(source,
'''const uint32_t SDHC_IRQSIGEN_MASK =
               SDHC_IRQSIGEN_DMAEIEN | SDHC_IRQSIGEN_AC12EIEN |
               SDHC_IRQSIGEN_DEBEIEN | SDHC_IRQSIGEN_DCEIEN |
               SDHC_IRQSIGEN_DTOEIEN | SDHC_IRQSIGEN_CIEIEN |
               SDHC_IRQSIGEN_CEBEIEN | SDHC_IRQSIGEN_CCEIEN |
               SDHC_IRQSIGEN_CTOEIEN | SDHC_IRQSIGEN_TCIEN;''',
'''// Command errors are consumed by the command poller, not this IRQ. Leaving their IRQ
// signals enabled while preserving their status would repeatedly preempt that poller.
const uint32_t SDHC_IRQSIGEN_MASK =
               SDHC_IRQSIGEN_DMAEIEN | SDHC_IRQSIGEN_AC12EIEN |
               SDHC_IRQSIGEN_DEBEIEN | SDHC_IRQSIGEN_DCEIEN |
               SDHC_IRQSIGEN_DTOEIEN | SDHC_IRQSIGEN_TCIEN;''')
    source = _replace(source,'static volatile uint32_t m_irqstat;',
'''static volatile uint32_t m_irqstat;
static volatile uint32_t m_cmdstat = 0;  // diagnostics; never overwrites latched data completion''')
    source = _replace(source,
'''static void sdIrs() {
  SDHC_IRQSIGEN = 0;
  m_irqstat = SDHC_IRQSTAT;
  SDHC_IRQSTAT = m_irqstat;
#if defined(__IMXRT1062__)
  SDHC_MIX_CTRL &= ~(SDHC_MIX_CTRL_AC23EN | SDHC_MIX_CTRL_DMAEN);
#endif
  m_dmaBusy = false;
}''',
'''static void sdIrs() {
  const uint32_t status = SDHC_IRQSTAT & SDHC_IRQSTAT_DATA_MASK;
  if (!status) return;  // a stale NVIC pending bit must not complete the next transfer
  m_irqstat |= status;
  SDHC_IRQSTAT = status;
  if (!(status & SDHC_IRQSTAT_DATA_TERMINAL)) return;  // DINT alone is not completion
  SDHC_IRQSIGEN = 0;
#if defined(__IMXRT1062__)
  SDHC_MIX_CTRL &= ~(SDHC_MIX_CTRL_AC23EN | SDHC_MIX_CTRL_DMAEN);
#endif
  m_dmaBusy = false;
}''')
    source = _replace(source,
'''static bool cardCommand(uint32_t xfertyp, uint32_t arg) {
  DBG_IRQSTAT();''',
'''static bool cardCommand(uint32_t xfertyp, uint32_t arg) {
  DBG_IRQSTAT();
  m_cmdstat = 0;''')
    source = _replace(source,
'''  m_irqstat = SDHC_IRQSTAT;
  SDHC_IRQSTAT = m_irqstat;

  return (m_irqstat & SDHC_IRQSTAT_CC) &&
         !(m_irqstat & SDHC_IRQSTAT_CMD_ERROR);''',
'''  const uint32_t cmdstat = SDHC_IRQSTAT & SDHC_IRQSTAT_CMD_MASK;
  m_cmdstat = cmdstat;
  SDHC_IRQSTAT = cmdstat;

  return (cmdstat & SDHC_IRQSTAT_CC) &&
         !(cmdstat & SDHC_IRQSTAT_CMD_ERROR);''')
    source = _replace(source,
'''static bool isBusyTransferComplete() {
  return !(SDHC_IRQSTAT & (SDHC_IRQSTAT_TC | SDHC_IRQSTAT_ERROR));
}''',
'''static bool isBusyTransferComplete() {
  return !(SDHC_IRQSTAT & SDHC_IRQSTAT_DATA_TERMINAL);
}''')
    source = _replace(source,
'''static void enableDmaIrs() {
  m_dmaBusy = true;
  m_irqstat = 0;
}''',
'''// Discard only stale data status at a new transfer boundary (e.g. R1b/abort TC).
// This must precede IRQ enable / command launch; a command completion remains its owner's.
static void clearDataStatus() {
  SDHC_IRQSIGEN = 0;
  SDHC_IRQSTAT = SDHC_IRQSTAT_DATA_MASK;
  m_irqstat = 0;
}
static void enableDmaIrs() {
  clearDataStatus();
  m_dmaBusy = true;
}''')
    source = _replace(source,
'''bool SdioCard::readStart(uint32_t sector) {
  DBG_IRQSTAT();
  if (yieldTimeout(isBusyCMD13)) {
    return sdError(SD_CARD_ERROR_CMD13);
  }
  SDHC_PROCTL |= SDHC_PROCTL_SABGREQ;''',
'''bool SdioCard::readStart(uint32_t sector) {
  DBG_IRQSTAT();
  if (yieldTimeout(isBusyCMD13)) {
    return sdError(SD_CARD_ERROR_CMD13);
  }
  clearDataStatus();
  SDHC_PROCTL |= SDHC_PROCTL_SABGREQ;''')
    source = _replace(source,
'''bool SdioCard::writeStart(uint32_t sector) {
  if (yieldTimeout(isBusyCMD13)) {
    return sdError(SD_CARD_ERROR_CMD13);
  }
  SDHC_PROCTL &= ~SDHC_PROCTL_SABGREQ;''',
'''bool SdioCard::writeStart(uint32_t sector) {
  if (yieldTimeout(isBusyCMD13)) {
    return sdError(SD_CARD_ERROR_CMD13);
  }
  clearDataStatus();
  SDHC_PROCTL &= ~SDHC_PROCTL_SABGREQ;''')
    source = _replace(source,
'''  m_irqstat = SDHC_IRQSTAT;
  SDHC_IRQSTAT = m_irqstat;''',
'''  m_irqstat = SDHC_IRQSTAT & SDHC_IRQSTAT_DATA_MASK;
  SDHC_IRQSTAT = m_irqstat;''',count=2)  # FIFO read and FIFO write completion
    source = _replace(source,
'''uint32_t SdioCard::errorData() const {
  return m_irqstat;
}''',
'''uint32_t SdioCard::errorData() const {
  return m_irqstat | m_cmdstat;
}''')
    return source


def digest(source):
    return hashlib.sha256(source.encode("utf-8")).hexdigest()


def patch_source(source):
    checksum = digest(source)
    if checksum == PATCHED_SHA256:
        return source
    if checksum != ORIGINAL_SHA256:
        raise RuntimeError("SDIO fix: unrecognized source SHA-256 " + checksum)
    patched = apply_dma_fix(source)
    if digest(patched) != PATCHED_SHA256:
        raise RuntimeError("SDIO fix: unexpected patched SHA-256 " + digest(patched))
    return patched


def framework_source(framework):
    metadata = json.loads((framework / "package.json").read_text(encoding="utf-8"))
    if not isinstance(metadata, dict):
        raise RuntimeError("SDIO fix: unknown framework metadata")
    if (metadata.get("name") != "framework-arduinoteensy" or
            metadata.get("version") != FRAMEWORK_VERSION):
        raise RuntimeError("SDIO fix: unsupported framework package " + repr(metadata))
    return framework / SOURCE_RELATIVE


def generated_path(framework, build):
    target = (build / "sdio_sdk_fix" / SOURCE_NAME).resolve()
    if framework == target or framework in target.parents:
        raise RuntimeError("SDIO fix: generated source must stay outside the SDK")
    if build not in target.parents:
        raise RuntimeError("SDIO fix: generated source is outside BUILD_DIR")
    return target


def prepare_copy(framework, build):
    framework, build = Path(framework).resolve(), Path(build).resolve()
    original = framework_source(framework)
    target = generated_path(framework, build)
    data = patch_source(original.read_bytes().decode("utf-8")).encode("utf-8")
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_bytes() != data:
        target.write_bytes(data)
    return original, target


def replacement_for(original, generated):
    compiled = None

    def replace(build_env, node):
        nonlocal compiled
        if node.name != SOURCE_NAME:
            return node
        source = Path(node.srcnode().get_abspath()).resolve()
        if source != original:
            raise RuntimeError("SDIO fix: unexpected source " + str(source))
        if compiled is None:
            # Keep the upstream object basename, replacing its member in incremental archives too.
            compiled = build_env.Object(
                target=str(generated) + ".o", source=build_env.File(str(generated)),
                CPPPATH=[str(original.parent)] + list(build_env.get("CPPPATH", [])))
            build_env.Depends(compiled, build_env.File(str(original)))
        return compiled

    return replace


def configure(env):
    platform = env.PioPlatform()
    if platform.name != "teensy":
        return
    framework = platform.get_package_dir("framework-arduinoteensy")
    if not framework:
        raise RuntimeError("SDIO fix: framework-arduinoteensy package is unavailable")
    original, generated = prepare_copy(framework, env.subst("$BUILD_DIR"))
    env.AddBuildMiddleware(replacement_for(original, generated))


# A normal Python import (including native tests without the SDK) must have no build side effects.
if "Import" in globals():
    Import("env")
    configure(env)
