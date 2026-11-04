"""Project-local SdFat source replacement; the installed framework stays read-only."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import runpy
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).absolute().parents[2]
SCRIPT = ROOT / "scripts/sdio_sdk_fix.py"
SOURCE_RELATIVE = Path("libraries/SdFat/src/SdCard/SdioTeensy.cpp")
ORIGINAL = "/* MIT License: fixture copyright and permission notice. */\nstatic int upstream;\n"
PATCHED = "/* MIT License: fixture copyright and permission notice. */\nstatic int fixed;\n"


# Pinned old anchors, recorded independently of the live framework and the patch implementation.
IRQ_SOURCE = r"""/**
 * Copyright (c) 2011-2021 Bill Greiman
 * This file is part of the SdFat library for SD memory cards.
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
const uint32_t SDHC_IRQSTAT_ERROR =
               SDHC_IRQSTAT_DMAE | SDHC_IRQSTAT_CMD_ERROR |
               SDHC_IRQSTAT_DATA_ERROR;
const uint32_t SDHC_IRQSIGEN_MASK =
               SDHC_IRQSIGEN_DMAEIEN | SDHC_IRQSIGEN_AC12EIEN |
               SDHC_IRQSIGEN_DEBEIEN | SDHC_IRQSIGEN_DCEIEN |
               SDHC_IRQSIGEN_DTOEIEN | SDHC_IRQSIGEN_CIEIEN |
               SDHC_IRQSIGEN_CEBEIEN | SDHC_IRQSIGEN_CCEIEN |
               SDHC_IRQSIGEN_CTOEIEN | SDHC_IRQSIGEN_TCIEN;
static volatile uint32_t m_irqstat;
static void sdIrs() {
  SDHC_IRQSIGEN = 0;
  m_irqstat = SDHC_IRQSTAT;
  SDHC_IRQSTAT = m_irqstat;
#if defined(__IMXRT1062__)
  SDHC_MIX_CTRL &= ~(SDHC_MIX_CTRL_AC23EN | SDHC_MIX_CTRL_DMAEN);
#endif
  m_dmaBusy = false;
}
static bool cardCommand(uint32_t xfertyp, uint32_t arg) {
  DBG_IRQSTAT();
  m_irqstat = SDHC_IRQSTAT;
  SDHC_IRQSTAT = m_irqstat;

  return (m_irqstat & SDHC_IRQSTAT_CC) &&
         !(m_irqstat & SDHC_IRQSTAT_CMD_ERROR);
static bool isBusyTransferComplete() {
  return !(SDHC_IRQSTAT & (SDHC_IRQSTAT_TC | SDHC_IRQSTAT_ERROR));
}
static void enableDmaIrs() {
  m_dmaBusy = true;
  m_irqstat = 0;
}
bool SdioCard::readStart(uint32_t sector) {
  DBG_IRQSTAT();
  if (yieldTimeout(isBusyCMD13)) {
    return sdError(SD_CARD_ERROR_CMD13);
  }
  SDHC_PROCTL |= SDHC_PROCTL_SABGREQ;
bool SdioCard::writeStart(uint32_t sector) {
  if (yieldTimeout(isBusyCMD13)) {
    return sdError(SD_CARD_ERROR_CMD13);
  }
  SDHC_PROCTL &= ~SDHC_PROCTL_SABGREQ;
  m_irqstat = SDHC_IRQSTAT;
  SDHC_IRQSTAT = m_irqstat;
  m_irqstat = SDHC_IRQSTAT;
  SDHC_IRQSTAT = m_irqstat;
uint32_t SdioCard::errorData() const {
  return m_irqstat;
}
"""
IRQ_PATCHED_SHA256 = 'fb5f87d27934c29e0b1f825b9c7098a2a27c9925fe13cad14b319785b660caee'


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


class Node:
    def __init__(self, path):
        self.path = Path(path)
        self.name = self.path.name

    def srcnode(self):
        return self

    def get_abspath(self):
        return str(self.path.absolute())


class Platform:
    def __init__(self, framework, name="teensy"):
        self.name, self.framework = name, framework
        self.queries = []

    def get_package_dir(self, name):
        self.queries.append(name)
        return str(self.framework) if self.framework else None


class Environment:
    def __init__(self, framework, build, platform="teensy"):
        self.platform = Platform(framework, platform)
        self.build = build
        self.middleware, self.objects, self.dependencies = [], [], []
        self.includes = ["existing/library/include"]

    def PioPlatform(self):
        return self.platform

    def subst(self, key):
        assert key == "$BUILD_DIR"
        return str(self.build)

    def AddBuildMiddleware(self, callback):
        self.middleware.append(callback)

    def Object(self, **options):
        result = [object()]
        self.objects.append((options, result))
        return result

    def File(self, path):
        return Node(path)

    def Depends(self, target, source):
        self.dependencies.append((target, source))

    def get(self, key, default):
        assert key == "CPPPATH"
        return self.includes


class SdioSdkFixTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.is_file(), "project-local SDIO replacement script is missing")
        spec = importlib.util.spec_from_file_location("sdio_sdk_fix_test", SCRIPT)
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.framework, self.build = self.root / "framework", self.root / "build"
        self.framework.mkdir()
        self.metadata = self.framework / "package.json"
        self.metadata.write_text(json.dumps({"name": "framework-arduinoteensy", "version": "1.162.0"}))
        self.original = self.framework / SOURCE_RELATIVE
        self.original.parent.mkdir(parents=True)
        self.original.write_text(ORIGINAL)

    def pin_fixture(self):
        changes = patch.multiple(self.module, ORIGINAL_SHA256=digest(ORIGINAL), PATCHED_SHA256=digest(PATCHED))
        changes.start()
        self.addCleanup(changes.stop)
        transform = patch.object(self.module, "apply_dma_fix", side_effect=lambda source: source.replace("upstream", "fixed"))
        mock = transform.start()
        self.addCleanup(transform.stop)
        return mock

    def test_known_original_patches_once_and_known_output_is_idempotent(self):
        transform = self.pin_fixture()
        self.assertEqual(PATCHED, self.module.patch_source(ORIGINAL))
        self.assertEqual(PATCHED, self.module.patch_source(PATCHED))
        transform.assert_called_once_with(ORIGINAL)
        self.assertEqual(ORIGINAL.splitlines()[0], PATCHED.splitlines()[0])

    def test_actual_patch_preserves_license_and_has_pinned_anchor_output(self):
        patched = self.module.apply_dma_fix(IRQ_SOURCE)
        self.assertEqual(IRQ_PATCHED_SHA256, digest(patched))
        notice = IRQ_SOURCE[:IRQ_SOURCE.index("const uint32_t")]
        self.assertTrue(patched.startswith(notice))
        self.assertEqual(1, patched.count("Copyright (c) 2011-2021 Bill Greiman"))
        self.assertEqual(1, patched.count(self.module.MARKER))
        self.assertEqual(2, patched.count("m_irqstat = SDHC_IRQSTAT & SDHC_IRQSTAT_DATA_MASK;"))
        with self.assertRaisesRegex(ValueError, "already patched"):
            self.module.apply_dma_fix(patched)

    def test_patch_anchor_is_strict_about_absence_and_ambiguity(self):
        self.assertEqual("new", self.module._replace("old", "old", "new"))
        self.assertEqual("new new", self.module._replace("old old", "old", "new", count=2))
        for source, old in [("unrecognized", "old"), ("old old", "old"), ("any", "")]:
            with self.subTest(source=source), self.assertRaises(ValueError):
                self.module._replace(source, old, "new")
        with self.assertRaises(ValueError):
            self.module.apply_dma_fix(IRQ_SOURCE.replace("static volatile uint32_t m_irqstat;", ""))

    def test_unknown_or_partial_source_is_rejected_before_transformation(self):
        transform = self.pin_fixture()
        for text in [ORIGINAL + "\n", PATCHED + "\n", "static int upstream;\n"]:
            with self.subTest(text=text), self.assertRaisesRegex(RuntimeError, "source SHA-256"):
                self.module.patch_source(text)
        transform.assert_not_called()

    def test_wrong_transformation_cannot_silently_compile(self):
        transform = self.pin_fixture()
        transform.side_effect = lambda source: source
        with self.assertRaisesRegex(RuntimeError, "patched SHA-256"):
            self.module.patch_source(ORIGINAL)

    def test_copy_is_local_preserves_original_and_does_not_rewrite_same_output(self):
        self.pin_fixture()
        original, generated = self.module.prepare_copy(self.framework, self.build)
        self.assertEqual(self.original, original)
        self.assertEqual(self.build / "sdio_sdk_fix/SdioTeensy.cpp", generated)
        self.assertEqual(PATCHED, generated.read_text())
        self.assertEqual(ORIGINAL, self.original.read_text())
        os.utime(generated, (123, 123))
        self.module.prepare_copy(self.framework, self.build)
        self.assertEqual(123000000000, generated.stat().st_mtime_ns)
        generated.write_text("stale output")
        self.module.prepare_copy(self.framework, self.build)
        self.assertEqual(PATCHED, generated.read_text())

    def test_unknown_framework_version_or_name_fails_closed(self):
        self.pin_fixture()
        for metadata in [{"name": "framework-arduinoteensy", "version": "2.0"},
                         {"name": "another-framework", "version": "1.162.0"}, []]:
            self.metadata.write_text(json.dumps(metadata))
            with self.subTest(metadata=metadata), self.assertRaisesRegex(RuntimeError, "framework"):
                self.module.prepare_copy(self.framework, self.build)
        self.assertFalse(self.build.exists())
        self.assertEqual(ORIGINAL, self.original.read_text())

    def test_build_output_inside_sdk_or_symlink_to_sdk_is_forbidden(self):
        self.pin_fixture()
        for target in [self.framework, self.framework / "nested"]:
            with self.subTest(target=target), self.assertRaisesRegex(RuntimeError, "outside|SDK"):
                self.module.prepare_copy(self.framework, target)
        parent = self.build / "sdio_sdk_fix"
        parent.mkdir(parents=True)
        (parent / "SdioTeensy.cpp").symlink_to(self.original)
        with self.assertRaisesRegex(RuntimeError, "outside|SDK"):
            self.module.prepare_copy(self.framework, self.build)
        self.assertEqual(ORIGINAL, self.original.read_text())

    def test_output_symlink_outside_build_directory_is_forbidden(self):
        self.pin_fixture()
        parent = self.build / "sdio_sdk_fix"
        parent.mkdir(parents=True)
        external = self.root / "external.cpp"
        external.write_text("untouched")
        (parent / "SdioTeensy.cpp").symlink_to(external)
        with self.assertRaisesRegex(RuntimeError, "outside"):
            self.module.prepare_copy(self.framework, self.build)
        self.assertEqual("untouched", external.read_text())

    def test_native_config_does_not_query_a_framework_package(self):
        env = Environment(None, self.build, platform="native")
        self.module.configure(env)
        self.assertFalse(env.platform.queries)
        self.assertFalse(env.middleware)
        self.assertFalse(self.build.exists())

    def test_missing_teensy_package_fails_closed(self):
        env = Environment(None, self.build)
        with self.assertRaisesRegex(RuntimeError, "framework"):
            self.module.configure(env)
        self.assertEqual(["framework-arduinoteensy"], env.platform.queries)

    def test_middleware_replaces_one_translation_unit_and_injects_only_local_includes(self):
        self.pin_fixture()
        env = Environment(self.framework, self.build)
        self.module.configure(env)
        self.assertEqual(1, len(env.middleware))
        callback = env.middleware[0]
        self.assertEqual(2, callback.__code__.co_argcount)  # PlatformIO's actual invocation contract
        result = callback(env, Node(self.original))
        self.assertIs(result, callback(env, Node(self.original)))
        self.assertEqual(1, len(env.objects))
        options = env.objects[0][0]
        generated = self.build / "sdio_sdk_fix/SdioTeensy.cpp"
        self.assertEqual(str(generated) + ".o", options["target"])
        self.assertEqual(generated, options["source"].path)
        self.assertEqual([str(self.original.parent), "existing/library/include"], options["CPPPATH"])
        self.assertEqual(["existing/library/include"], env.includes)
        self.assertTrue(any(node.path == self.original for _, node in env.dependencies))
        self.assertEqual(ORIGINAL, self.original.read_text())

    def test_middleware_leaves_other_sources_and_rejects_unknown_same_name(self):
        self.pin_fixture()
        env = Environment(self.framework, self.build)
        self.module.configure(env)
        callback = env.middleware[0]
        other = Node(self.root / "other.cpp")
        self.assertIs(other, callback(env, other))
        with self.assertRaisesRegex(RuntimeError, "unexpected"):
            callback(env, Node(self.root / "SdioTeensy.cpp"))
        self.assertFalse(env.objects)

    def test_scons_entry_is_optional_on_regular_python_import(self):
        env = Environment(None, self.build, platform="native")
        importer = Mock()
        runpy.run_path(str(SCRIPT), init_globals={"Import": importer, "env": env})
        importer.assert_called_once_with("env")
        self.assertFalse(env.platform.queries)

    def test_scons_globals_without_file_can_construct_the_replacement_object(self):
        # SCons executes pre-scripts after explicitly removing __file__ from their globals.
        namespace = {"__name__": "sdio_scons_no_file"}
        exec(compile(SCRIPT.read_text(), str(SCRIPT), "exec"), namespace)
        namespace.update(ORIGINAL_SHA256=digest(ORIGINAL), PATCHED_SHA256=digest(PATCHED),
                         apply_dma_fix=lambda source: source.replace("upstream", "fixed"))
        env = Environment(self.framework, self.build)
        namespace["configure"](env)
        try:
            env.middleware[0](env, Node(self.original))
        except Exception as exc:
            self.fail("SCons source replacement failed without __file__: %r" % exc)
        self.assertEqual(1, len(env.objects))


if __name__ == "__main__":
    unittest.main()
