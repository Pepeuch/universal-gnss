#!/usr/bin/env python3
"""Offline source distributions from immutable Git trees. See README.md for the schema."""

from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

GENERATOR_VERSION = 1
METADATA = '.ug-distribution.json'
INVENTORY = '.ug-inventory.json'
MANIFEST = '.ug-manifest.yaml'
README = 'README.distribution.md'
RESERVED = {METADATA, INVENTORY, MANIFEST, README}


def git_environment():
    # Partial clones must fail on missing blobs instead of lazily fetching.
    # Replacement refs must not change the contents attributed to a source SHA.
    return {**os.environ, 'GIT_NO_LAZY_FETCH': '1', 'GIT_NO_REPLACE_OBJECTS': '1',
            'GIT_TERMINAL_PROMPT': '0'}


class DistributionError(Exception):
    """An input or generated tree violates the distribution contract."""


def require(condition, message):
    if not condition:
        raise DistributionError(message)


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + '\n').encode()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f'duplicate manifest/metadata key: {key}')
        result[key] = value
    return result


def decode(data):
    # JSON is a strict subset of YAML 1.2; no external parser or network needed.
    try:
        return json.loads(data, object_pairs_hook=unique_object)
    except (ValueError, UnicodeError) as error:
        raise DistributionError(f'invalid JSON-subset YAML/JSON: {error}') from error


def safe_path(value):
    require(isinstance(value, str) and value and not any(c in value for c in '\\\n\r\x00'),
            f'invalid path: {value!r}')
    path = PurePosixPath(value)
    require(not path.is_absolute() and all(p not in ('', '.', '..', '.git') for p in value.rstrip('/').split('/')),
            f'unsafe path: {value!r}')
    return value


def matches(path, rule):
    return path.startswith(rule) if rule.endswith('/') else path == rule


class Snapshot:
    def __init__(self, repository, ref):
        self.repo = Path(repository).resolve()
        self.repo = Path(self.git('rev-parse', '--show-toplevel').decode().strip()).resolve()
        self.commit = self.git('rev-parse', '--verify', ref + '^{commit}').decode().strip()
        require(re.fullmatch(r'[0-9a-f]{40,64}', self.commit), 'invalid source commit')
        self.tree = {}
        for record in self.git('ls-tree', '-rz', '--full-tree', self.commit).split(b'\0'):
            if not record:
                continue
            info, raw_path = record.split(b'\t', 1)
            mode, kind, oid = info.decode().split()
            path = safe_path(raw_path.decode('utf-8'))
            require(kind == 'blob', f'unsupported Git entry (submodule): {path}')
            self.tree[path] = (mode, oid)
        self.cache = {}

    def git(self, *args):
        result = subprocess.run(['git', '-c', 'protocol.allow=never', '-C', str(self.repo), *args],
                                capture_output=True, env=git_environment())
        require(result.returncode == 0, f'Git failed: {result.stderr.decode(errors="replace").strip()}')
        return result.stdout

    def read_many(self, paths):
        pending = sorted(set(paths) - self.cache.keys())
        if not pending:
            return
        result = subprocess.run(['git', '-c', 'protocol.allow=never', '-C', str(self.repo), 'cat-file', '--batch'],
                                input=''.join(self.tree[p][1] + '\n' for p in pending).encode(),
                                capture_output=True, env=git_environment())
        require(result.returncode == 0, 'Git blob read failed offline: '
                + result.stderr.decode(errors='replace').strip())
        offset = 0
        for path in pending:
            end = result.stdout.index(b'\n', offset)
            header = result.stdout[offset:end].split()
            require(len(header) == 3, f'Git blob unavailable offline: {path}')
            _, kind, length = header
            require(kind == b'blob', f'not a blob: {path}')
            offset = end + 1
            self.cache[path] = result.stdout[offset:offset + int(length)]
            offset += int(length) + 1

    def read(self, path):
        require(path in self.tree, f'source path missing: {path}')
        self.read_many([path])
        return self.cache[path]


def validate_manifest(manifest, snapshot):
    require(isinstance(manifest, dict), 'manifest must be an object')
    require(set(manifest) == {'schema_version', 'ug_version', 'components', 'distributions',
                              'exclude', 'references'}, 'unknown or missing manifest field')
    require(manifest['schema_version'] == 1, 'unsupported manifest schema')
    require(isinstance(manifest['ug_version'], str) and manifest['ug_version'], 'UG version missing')
    components = manifest['components']
    profiles = manifest['distributions']
    require(isinstance(components, dict) and components and isinstance(profiles, dict) and profiles,
            'components/distributions must be nonempty mappings')
    rules = []
    for name, component in components.items():
        require(re.fullmatch(r'[a-z][a-z0-9_]*', name), f'invalid component identity: {name}')
        require(set(component) == {'paths', 'requires', 'external', 'platforms', 'frameworks',
                                   'hardware_profiles', 'distributions'}, f'component schema: {name}')
        for field in ('paths', 'requires', 'external', 'platforms', 'frameworks',
                      'hardware_profiles', 'distributions'):
            values = component[field]
            require(isinstance(values, list) and all(isinstance(v, str) and v for v in values)
                    and len(values) == len(set(values)), f'invalid {field}: {name}')
        require(component['paths'], f'component without paths: {name}')
        for dep in component['requires']:
            require(dep in components, f'unknown dependency: {name} -> {dep}')
        require(set(component['distributions']) <= profiles.keys(), f'unknown distribution in {name}')
        for path in component['paths']:
            safe_path(path)
            require(any(matches(p, path) for p in snapshot.tree), f'path does not exist: {name}: {path}')
            rules.append((path, name))
    # Check the entire graph, including unselected optional components.
    state = {}
    def visit(name, chain):
        require(state.get(name) != 1, 'dependency cycle: ' + ' -> '.join(chain + [name]))
        if state.get(name) == 2:
            return
        state[name] = 1
        for dep in components[name]['requires']:
            visit(dep, chain + [name])
        state[name] = 2
    for name in components:
        visit(name, [])
    require(isinstance(manifest['exclude'], list), 'exclude must be a list')
    for entry in manifest['exclude']:
        require(set(entry) == {'path', 'reason'} and isinstance(entry['reason'], str)
                and entry['reason'], 'exclusion needs a path and reason')
        rules.append((safe_path(entry['path']), None))
    owners = {}
    for path in snapshot.tree:
        candidates = [(len(rule), rule, owner) for rule, owner in rules if matches(path, rule)]
        require(candidates, f'unclassified source file: {path}')
        length = max(c[0] for c in candidates)
        winners = [c for c in candidates if c[0] == length]
        require(len(winners) == 1, f'ambiguous classification: {path}')
        owners[path] = winners[0][2]
    for name, profile in profiles.items():
        require(set(profile) == {'platform', 'framework', 'hardware_profile', 'ros_distro',
                                 'roots', 'options', 'forbidden'}, f'profile schema: {name}')
        require(profile['ros_distro'] in ('kilted', 'lyrical'), f'unqualified ROS distro: {name}')
        require(profile['platform'] == 'linux' and profile['framework'] == 'ros2',
                f'unqualified distribution platform/framework: {name}')
        require(isinstance(profile['hardware_profile'], str) and profile['hardware_profile'],
                f'invalid hardware profile: {name}')
        require(isinstance(profile['roots'], list) and profile['roots'], f'roots missing: {name}')
        require(isinstance(profile['options'], dict), f'options must be a mapping: {name}')
        for root in profile['roots'] + list(profile['options'].values()):
            require(root in components, f'unknown root/option component: {name}: {root}')
        require(isinstance(profile['forbidden'], list), f'forbidden must be a list: {name}')
        for path in profile['forbidden']:
            if path != '.git/':
                safe_path(path)
    require(isinstance(manifest['references'], list), 'references must be a list')
    for edge in manifest['references']:
        require(set(edge) == {'from', 'to', 'reason'} and isinstance(edge['reason'], str)
                and edge['reason'], 'reference requires from/to/reason')
        for field in ('from', 'to'):
            safe_path(edge[field])
            require(any(matches(p, edge[field]) for p in snapshot.tree),
                    f'reference path does not exist: {edge[field]}')
    return owners


def closure(manifest, profile_name, options, excluded):
    require(profile_name in manifest['distributions'], f'unknown distribution: {profile_name}')
    profile = manifest['distributions'][profile_name]
    selected = set()
    require(len(options) == len(set(options)), 'duplicate option')
    roots = list(profile['roots'])
    for option in options:
        require(option in profile['options'], f'unknown option: {option}')
        roots.append(profile['options'][option])
    def visit(name):
        if name in selected:
            return
        component = manifest['components'][name]
        require(profile_name in component['distributions'], f'distribution incompatible: {name}')
        for field, value in [('platforms', profile['platform']), ('frameworks', profile['framework']),
                             ('hardware_profiles', profile['hardware_profile'])]:
            require(value in component[field], f'platform/framework/hardware incompatibility: {name}: {value}')
        selected.add(name)
        for dep in component['requires']:
            visit(dep)
    for name in roots:
        visit(name)
    require(set(excluded) <= manifest['components'].keys(), 'unknown excluded component')
    require(not selected.intersection(excluded),
            'cannot exclude required dependency: ' + ', '.join(sorted(selected.intersection(excluded))))
    return sorted(selected)


def cmake_plugin_declarations(text):
    """Read literal declarations, not CMake execution/variables/control flow.

    Tokenize quotes and bracket arguments before recognizing commands so that
    line/bracket comments and strings cannot manufacture a declaration.
    Backslash/dynamic arguments are retained for explicit rejection when needed.
    """
    tokens = []
    index = 0
    while index < len(text):
        char = text[index]
        if char.isspace():
            index += 1
            continue
        comment = char == '#'
        bracket = re.match(r'\[(=*)\[', text[index + int(comment):])
        if bracket:
            start = index + int(comment) + bracket.end()
            end = text.find(']' + bracket[1] + ']', start)
            require(end >= 0, 'unsupported CMake syntax: unclosed bracket')
            if not comment:
                # CMake discards the first newline after a bracket opener.
                value = re.sub(r'^\r?\n', '', text[start:end], count=1)
                tokens.append(('argument', value))
            index = end + len(bracket[1]) + 2
        elif comment:
            end = text.find('\n', index)
            index = len(text) if end < 0 else end + 1
        elif char == '"':
            quoted = re.match(r'"((?:\\.|[^"\\])*)"', text[index:], re.S)
            require(quoted is not None, 'unsupported CMake syntax: unclosed quote')
            tokens.append(('argument', quoted[1]))
            index += quoted.end()
        elif char in '()':
            tokens.append((char, char))
            index += 1
        else:
            word = re.match(r'[^\s()#"]+', text[index:])[0]
            tokens.append(('word', word))
            index += len(word)
    declarations = {'add_library': [], 'pluginlib_export_plugin_description_file': []}
    index = 0
    while index + 1 < len(tokens):
        kind, name = tokens[index]
        if kind != 'word' or tokens[index + 1][0] != '(':
            index += 1
            continue
        index += 2
        depth, arguments = 1, []
        while index < len(tokens) and depth:
            kind, value = tokens[index]
            depth += (kind == '(') - (kind == ')')
            if depth:
                arguments.append(value)
            index += 1
        require(depth == 0, 'unsupported CMake syntax: unclosed command')
        if name.lower() in declarations:
            declarations[name.lower()].append(arguments)
    return declarations


def check_references(snapshot, manifest, paths, owners, selected):
    """Check explicit audited edges plus supported literal source references.

    This is deliberately not a general CMake/Python evaluator. Isolated builds
    exercise computed paths; new computed cross-component edges need manifest entries.
    """
    def present(target, origin):
        target = os.path.normpath(target).removeprefix('./')
        require(not target.startswith('../') and not target.startswith('/'),
                f'internal reference escapes distribution: {origin}: {target}')
        require(target in paths or any(p.startswith(target.rstrip('/') + '/') for p in paths),
                f'internal reference not distributed: {origin} -> {target}')
    for edge in manifest['references']:
        if edge['from'] in paths:
            present(edge['to'], edge['from'])
    header_index = defaultdict(list)
    for path in snapshot.tree:
        if '/include/' in path:
            header_index[path.split('/include/', 1)[1]].append(path)
    packages, selected_packages = {}, {}
    for path in snapshot.tree:
        if path.endswith('/package.xml') and owners[path] is not None:
            name = ET.fromstring(snapshot.read(path)).findtext('name')
            if path in paths:
                require(name not in selected_packages,
                        f'duplicate ROS package: {name}: {selected_packages.get(name)} and {path}')
                selected_packages[name] = path
            # Keep identification of undistributed internal dependencies, without
            # letting an unselected duplicate replace a selected package.
            packages.setdefault(name, path)
    packages.update(selected_packages)
    source_roots = {p.split('/')[0] for p in snapshot.tree if '/' in p}
    root_pattern = '|'.join(re.escape(p) for p in sorted(source_roots, key=len, reverse=True))
    # Only syntactic path tokens, not arbitrary prose or installed /opt/... paths.
    root_refs = re.compile(r'(?:(?<![\w/.-])|(?<=\}/))(' + root_pattern + r')/([\w./-]+)')
    for path in sorted(paths):
        data = snapshot.read(path)
        if not (path.endswith(('.cpp', '.hpp', '.h', '.py', '.sh', '.xml', '.yaml'))
                or path.endswith('CMakeLists.txt') or path == 'Dockerfile'):
            continue
        text = re.sub(r'^\s*#(?!\s*include)[^\n]*$', '', data.decode('utf-8'), flags=re.M)
        component = manifest['components'][owners[path]]
        if path.endswith('/package.xml'):
            for export in ET.fromstring(data).findall('export/*'):
                plugin = export.get('plugin')
                if plugin is None:
                    continue
                require(plugin.startswith('${prefix}/'), f'unsupported plugin path: {path}: {plugin}')
                relative = safe_path(plugin.removeprefix('${prefix}/'))
                descriptor = str(PurePosixPath(path).parent / relative)
                present(descriptor, path)
                cmake = snapshot.read(str(PurePosixPath(path).parent / 'CMakeLists.txt')).decode('utf-8')
                declarations = cmake_plugin_declarations(cmake)
                exports = declarations['pluginlib_export_plugin_description_file']
                dynamic = lambda value: any(marker in value for marker in ('$', '\\', ';'))
                require(not any(dynamic(value) for args in exports for value in args[:2]),
                        f'cannot statically prove dynamic plugin export: {descriptor}')
                require([export.tag, relative] in exports,
                        f'plugin descriptor not exported by CMake: {descriptor}')
                libraries = list(ET.fromstring(snapshot.read(descriptor)).iter('library'))
                require(libraries, f'plugin descriptor contains no library: {descriptor}')
                for library in libraries:
                    definitions = declarations['add_library']
                    targets = [args for args in definitions if args and args[0] == library.get('path')]
                    require(targets or not any(args and dynamic(args[0]) for args in definitions),
                            f'cannot statically prove dynamic plugin target: {descriptor}')
                    require(targets,
                            f'plugin library target missing: {descriptor}: {library.get("path")}')
                    require(len(targets) == 1 and len(targets[0]) > 1,
                            f'cannot statically prove plugin library type: {descriptor}')
                    require(not dynamic(targets[0][1]),
                            f'cannot statically prove dynamic plugin library type: {descriptor}')
                    require(targets[0][1] in ('SHARED', 'MODULE') and 'IMPORTED' not in targets[0][2:],
                            f'plugin library is not a built dynamic library: {descriptor}')
        if path.endswith('CMakeLists.txt') or path.endswith('/package.xml'):
            dependencies = re.findall(r'find_package\(\s*([\w_]+)', text)
            if path.endswith('/package.xml'):
                dependencies += [e.text for e in ET.fromstring(data)
                                 if e.tag.endswith('_depend') or e.tag == 'depend']
            external_names = {item.split()[0] for item in component['external']}
            for dep in dependencies:
                if dep in packages:
                    present(packages[dep], path)
                else:
                    require(dep in external_names, f'undeclared external dependency: {path}: {dep}')
        for match in root_refs.finditer(text):
            target = match.group(1) + '/' + match.group(2).rstrip('/.')
            # ROSIDL emits snake_case C++ headers from CamelCase interface files.
            generated = re.fullmatch(r'universal_gnss_msgs/(msg|srv)/([a-z_]+)\.hpp', target)
            if generated:
                interface = ''.join(word.title() for word in generated[2].split('_'))
                target = f'universal_gnss_msgs/{generated[1]}/{interface}.{generated[1]}'
            # A path prefix embedded in a longer quoted name still needs coverage.
            present(target, path)
        if path.endswith('CMakeLists.txt'):
            for token in re.findall(r'\$\{(?:UNIVERSAL_GNSS_SOURCE_ROOT|CMAKE_SOURCE_DIR|PROJECT_SOURCE_DIR)\}/([\w./-]+)', text):
                present(token, path)
            for token in re.findall(r'(?<![\w/])(?:src|tests|include|launch|config|msg|srv)/[\w./-]+', text):
                present(str(PurePosixPath(path).parent / token), path)
            for token in re.findall(r'\$\{CMAKE_CURRENT_SOURCE_DIR\}/([\w./-]+)', text):
                present(str(PurePosixPath(path).parent / token), path)
        if path.endswith(('.cpp', '.hpp', '.h')):
            for delimiter, header in re.findall(r'#\s*include\s*(["<])([^">]+)[">]', text):
                candidates = header_index.get(header, [])
                local = str(PurePosixPath(path).parent / header)
                if delimiter == '"' and local in snapshot.tree:
                    candidates = [local]
                elif delimiter == '"' and '/' not in header:
                    # CMake also adds shared test helper directories to include paths.
                    candidates = [p for p in snapshot.tree if p.endswith('/' + header)]
                if candidates:
                    require(any(p in paths for p in candidates), f'include not distributed: {path}: {header}')
                elif header.split('/')[0] in {name.split('/')[0] for name in header_index} or (
                        delimiter == '"' and '/' not in header):
                    raise DistributionError(f'internal include missing: {path}: {header}')
                # ROSIDL headers are generated; require their source interface.
                match = re.fullmatch(r'universal_gnss_msgs/(msg|srv)/([a-z_]+)\.hpp', header)
                if match:
                    interface = ''.join(word.title() for word in match[2].split('_'))
                    present(f'universal_gnss_msgs/{match[1]}/{interface}.{match[1]}', path)
        if path == 'Dockerfile':
            for line in text.splitlines():
                if line.startswith('COPY ') and '--from=' not in line:
                    source = line.split()[1:]
                    source = [s for s in source if not s.startswith('--')][0]
                    if source != '.':
                        present(source, path)
    # Every selected component must actually contribute files.
    require(set(selected) <= {owners[p] for p in paths}, 'selected component contributes no files')


def contract_path(path):
    return ('/include/' in path or '/msg/' in path or '/srv/' in path or '/launch/' in path
            or '/config/' in path or path.endswith(('package.xml', 'CMakeLists.txt', '_plugins.xml')))


def plan(snapshot, manifest_bytes, profile_name, options=(), excluded=()):
    manifest = decode(manifest_bytes)
    owners = validate_manifest(manifest, snapshot)
    selected = closure(manifest, profile_name, options, excluded)
    profile = manifest['distributions'][profile_name]
    paths = {p for p, owner in owners.items() if owner in selected}
    for path in paths:
        require(path not in RESERVED, f'reserved output name: {path}')
        require(snapshot.tree[path][0] in ('100644', '100755'), f'symlink/unsupported mode: {path}')
        require(not any(matches(path, rule) for rule in profile['forbidden']), f'forbidden file: {path}')
    snapshot.read_many(paths)
    check_references(snapshot, manifest, paths, owners, selected)
    records = [{'path': p, 'sha256': digest(snapshot.read(p)), 'mode': snapshot.tree[p][0],
                'component': owners[p], 'public_contract': contract_path(p)} for p in sorted(paths)]
    packages = {ET.fromstring(snapshot.read(p)).findtext('name'): ET.fromstring(snapshot.read(p)).findtext('version')
                for p in paths if p.endswith('/package.xml')}
    external = sorted({dep for name in selected for dep in manifest['components'][name]['external']})
    metadata = {'schema_version': 1, 'generator_version': GENERATOR_VERSION,
                'generator_sha256': digest(Path(__file__).read_bytes()),
                'source_commit': snapshot.commit, 'distribution': profile_name,
                'options': sorted(options), 'components': selected, 'platform': profile['platform'],
                'framework': profile['framework'], 'hardware_profile': profile['hardware_profile'],
                'ros_distro': profile['ros_distro'], 'ug_version': manifest['ug_version'],
                'package_versions': packages, 'external_dependencies': external,
                'manifest_sha256': digest(manifest_bytes), 'inventory_sha256': digest(json_bytes(records)),
                'publication': 'local-source-tree-only'}
    distro = profile['ros_distro']
    package_names = ['universal_gnss_msgs', 'universal_gnss_ros2']
    package_paths = ['universal_gnss_msgs', 'gnss_ros2']
    if 'mavros' in selected:
        package_names.append('universal_gnss_mavros')
        package_paths.append('adapters/mavros/gnss_mavros')
    instructions = f'''# Universal GNSS source distribution: {profile_name}

Source commit: `{snapshot.commit}`. UG functional version: `{manifest['ug_version']}`.
Generator format: {GENERATOR_VERSION}. This is a local generated source tree, not a published release
or a new physical qualification. Package versions are preserved verbatim in package.xml.

Install the external dependencies listed in {METADATA} before building.
Use a clean environment with ROS2 {distro}; do not source an old UG installation.
Build as a normal user in a separate workspace (this directory is the source tree):

```bash
source /opt/ros/{distro}/setup.bash
colcon --log-base /tmp/ug-{profile_name}/log build --build-base /tmp/ug-{profile_name}/build --install-base /tmp/ug-{profile_name}/install --base-paths {' '.join(package_paths)} --packages-select {' '.join(package_names)} --cmake-args -DCMAKE_BUILD_TYPE=Release
source /tmp/ug-{profile_name}/install/setup.bash
colcon --log-base /tmp/ug-{profile_name}/log test --build-base /tmp/ug-{profile_name}/build --packages-select {' '.join(package_names)} --return-code-on-test-failure
colcon test-result --test-result-base /tmp/ug-{profile_name}/build --verbose
```

The optional MAVROS plugin requires MAVROS 2.16.0 and its declared dependencies;
source its external installation before building if selected. Docker packaging,
when selected, preserves the original Dockerfile and COPY paths and needs the
normal ROS base images/system packages. Generation itself uses no network.

Only package-based ROS2 builds are promised. The full repository's root native
CMake entry point and publication workflows are deliberately not distributed.
{INVENTORY} lists every copied source file, mode, owner and content hash.
{MANIFEST} records the exact input manifest. Verify with the generator's
verify_backend.sh against the trusted source repository and explicit source SHA;
verification recomputes the expected tree rather than trusting an editable inventory.
'''
    output = {p: (snapshot.read(p), snapshot.tree[p][0]) for p in sorted(paths)}
    for path, content in [(METADATA, json_bytes(metadata)), (INVENTORY, json_bytes(records)),
                          (MANIFEST, manifest_bytes), (README, instructions.encode())]:
        output[path] = (content, '100644')
    return output


def inspect_output(destination):
    require(destination.is_dir() and not destination.is_symlink(), 'output must be a real directory')
    found = {}
    for base, dirs, files in os.walk(destination, followlinks=False):
        for name in dirs + files:
            path = Path(base) / name
            require(not path.is_symlink(), f'output symlink forbidden: {path}')
        for name in files:
            path = Path(base) / name
            require(path.is_file(), f'non-regular output file: {path}')
            relative = path.relative_to(destination).as_posix()
            mode = '100755' if path.stat().st_mode & 0o111 else '100644'
            found[relative] = (path.read_bytes(), mode)
    return found


def verify(destination, expected):
    found = inspect_output(destination)
    require(found.keys() == expected.keys(),
            f'file inventory mismatch; missing={sorted(expected.keys() - found.keys())}, '
            f'unexpected/forbidden={sorted(found.keys() - expected.keys())}')
    for path, item in expected.items():
        require(found[path] == item, f'content/mode or public contract mismatch: {path}')
    expected_dirs = {str(parent) for path in expected for parent in PurePosixPath(path).parents
                     if str(parent) != '.'}
    actual_dirs = {p.relative_to(destination).as_posix() for p in destination.rglob('*') if p.is_dir()}
    require(actual_dirs == expected_dirs, 'unexpected or missing directory in distribution')


def generate(snapshot, output, destination=None):
    if destination is None:
        destination = Path(tempfile.mkdtemp(prefix='ug-distribution-'))
        destination.rmdir()  # Reserve a unique name; the same non-overwrite rule applies.
    else:
        destination = Path(destination).absolute()
    require(not destination.is_symlink(), 'output symlink forbidden')
    destination = destination.resolve()
    require(not destination.is_relative_to(snapshot.repo), 'output inside source repository forbidden')
    require(not destination.exists(), 'destination already exists; refusing to overwrite any data')
    require(destination.parent.is_dir(), 'output parent does not exist')
    destination.mkdir(mode=0o755)
    try:
        for path, (content, mode) in output.items():
            target = destination / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
            target.chmod(0o755 if mode == '100755' else 0o644)
        verify(destination, output)
    except BaseException:
        shutil.rmtree(destination)
        raise
    return destination


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['make', 'verify'])
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--source-ref', help='Explicit source commit/ref (required for generation)')
    parser.add_argument('--manifest', type=Path, help='JSON-subset YAML manifest; default: this tool manifest')
    parser.add_argument('--distribution', help='ros2-kilted or ros2-lyrical')
    parser.add_argument('--with', action='append', default=[], dest='options', help='Optional component: mavros or docker')
    parser.add_argument('--exclude', action='append', default=[], help='Assert a component is unnecessary; fail if required')
    parser.add_argument('--output', type=Path, help='New destination for make; existing source tree for verify')
    args = parser.parse_args(argv)
    try:
        if args.operation == 'verify':
            require(args.output is not None, 'verify requires --output')
            # Reject symlinks before reading even the metadata.
            actual = inspect_output(args.output)
            require(METADATA in actual and MANIFEST in actual, 'generated metadata/manifest missing')
            metadata = decode(actual[METADATA][0])
            ref = args.source_ref or metadata['source_commit']
            profile = args.distribution or metadata['distribution']
            options = args.options or metadata['options']
            # The output is untrusted. Compare its embedded manifest against the
            # caller's trusted manifest, never use it as verification authority.
            manifest_bytes = (args.manifest or Path(__file__).with_name('manifest.yaml')).read_bytes()
        else:
            require(args.source_ref is not None and args.distribution is not None,
                    'make requires --source-ref and --distribution')
            ref, profile, options = args.source_ref, args.distribution, args.options
            manifest_bytes = (args.manifest or Path(__file__).with_name('manifest.yaml')).read_bytes()
        snapshot = Snapshot(args.repo, ref)
        expected = plan(snapshot, manifest_bytes, profile, options, args.exclude)
        if args.operation == 'make':
            print(generate(snapshot, expected, args.output))
        else:
            verify(args.output, expected)
            print(f'PASS: {profile}, source {snapshot.commit}, {len(expected)} files')
    except (DistributionError, OSError, ValueError, KeyError, TypeError, ET.ParseError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
