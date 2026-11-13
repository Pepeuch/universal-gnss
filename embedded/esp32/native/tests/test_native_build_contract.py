# SPDX-License-Identifier: LGPL-3.0-only
"""Regression for the actual native compile-command language gate."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('native_verify', Path(__file__).resolve().parents[1]/'verify.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class NativeBuildContract(unittest.TestCase):
    def test_project_standards_are_effective_not_minimum_or_overridden(self):
        commands = [
            {'file': '/ug/core/lib.cpp', 'command': 'g++ -std=gnu++26 -std=gnu++17 -c /ug/core/lib.cpp'},
            {'file': '/ug/embedded/esp32/components/radio/radio.c', 'command': 'gcc -std=gnu23 -std=gnu17 -c radio.c'},
            {'file': '/build/project/main/gateway.cpp', 'command': 'g++ -std=c++17 -c gateway.cpp'},
            {'file': '/sdk/file.cpp', 'command': 'g++ -std=gnu++26 -c file.cpp'}]
        checked = module.project_standards(commands, '/ug', '/build/project/main')
        self.assertEqual(checked, {'c': ['gnu17'], 'cxx': ['c++17', 'gnu++17']})
        for invalid in ['g++ -std=gnu++17 -std=gnu++26 -c lib.cpp', 'g++ -c lib.cpp']:
            with self.assertRaises(ValueError):
                module.project_standards([{'file': '/ug/core/lib.cpp', 'command': invalid}], '/ug', '/build/project/main')
        with self.assertRaises(ValueError):
            module.project_standards([{'file': '/ug/embedded/esp32/components/radio/radio.c',
                                      'command': 'gcc -std=gnu23 -c radio.c'}], '/ug', '/build/project/main')


if __name__ == '__main__':
    unittest.main()
