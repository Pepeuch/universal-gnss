"""Real Chromium fixture checks; no device, flash or live API access.

Host-only dependency: playwright==1.56.0 and its Chromium image. Public assets
and synthetic API fixtures are intercepted at https://ug.test; external requests
are refused. Run with --artifacts outside the source tree for optional previews.
"""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
from urllib.parse import urlparse
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[4]
ASSETS = ROOT / 'embedded/esp32/components/ug_web/assets'
spec = importlib.util.spec_from_file_location('pack', ASSETS.parent / 'tools/pack_assets.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


def run(artifacts=None):
    with sync_playwright() as engine:
        browser = engine.chromium.launch(headless=True)
        for gnss, width in ((True, 1280), (True, 390), (False, 390)):
            context = browser.new_context(ignore_https_errors=True, viewport={'width': width, 'height': 900})
            page = context.new_page()
            calls, patches, errors, pending = [], [], [], set()
            peak = [0]
            config = {'schema': 1, 'input': 'ntrip', 'gnss': {'enabled': False, 'tx_pin': -1, 'rx_pin': -1, 'baud': 115200, 'model': 'auto'},
                      'upstream': {'enabled': False, 'endpoint': {'host': 'caster.test', 'port': 2101, 'mountpoint': 'IN', 'password_set': True}},
                      'publishers': [], 'radio': {'enabled': False},
                      'caster': {'enabled': False, 'port': 2101, 'mountpoint': 'LOCAL'},
                      'wifi': {'sta': False, 'ap': False, 'ssid': '', 'ap_ssid': 'UG-RTK'}}
            receiver = {'present': False, 'model_verified': False, 'model': '', 'position_stale': True, 'fix_valid': False}
            rejected, offline = [False], [False]

            def request_started(request):
                if '/api/' in request.url:
                    pending.add(request)
                    peak[0] = max(peak[0], len(pending))

            page.on('request', request_started)
            page.on('requestfinished', lambda request: pending.discard(request))
            page.on('requestfailed', lambda request: pending.discard(request))
            page.on('pageerror', lambda error: errors.append(str(error)))
            page.on('dialog', lambda dialog: dialog.accept('SAVE_RECEIVER_CONFIGURATION' if dialog.type == 'prompt' else None))

            def route_handler(route):
                url = urlparse(route.request.url)
                if url.netloc != 'ug.test':
                    route.abort()
                    return
                path = url.path
                if not path.startswith('/api/'):
                    name = {'/': 'index.html', '/app.js': 'app.js', '/style.css': 'style.css'}.get(path)
                    if name is None:
                        route.fulfill(status=404, body='not found')
                        return
                    route.fulfill(body=pack.source_bytes(ASSETS, name), content_type={'index.html': 'text/html', 'app.js': 'application/javascript', 'style.css': 'text/css'}[name],
                                  headers={'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self'; frame-ancestors 'none'; base-uri 'none'"})
                    return
                calls.append((path, route.request.method))
                if offline[0] and path == '/api/resources':
                    route.fulfill(status=503, body='fixture disconnected')
                    return
                if path not in ('/api/resources', '/api/status', '/api/capabilities') and route.request.headers.get('authorization') != 'Bearer ' + 'z' * 64:
                    route.fulfill(status=401, body='administrator required')
                    return
                if path == '/api/config' and route.request.method == 'POST':
                    value = route.request.post_data_json
                    patches.append(value)
                    if rejected[0]:
                        route.fulfill(status=400, body='invalid fixture configuration')
                        return
                    for key, item in value.items():
                        if isinstance(item, dict):
                            config.setdefault(key, {}).update(item)
                        else:
                            config[key] = item
                    value = {'saved': True}
                elif path == '/api/config':
                    value = copy.deepcopy(config)
                    for owner in (value.get('upstream', {}), value.get('caster', {})):
                        owner.pop('password', None)
                        owner.get('endpoint', {}).pop('password', None)
                elif path == '/api/capabilities':
                    value = {'gnss': gnss, 'publishers': 1, 'gnss_allowed_pins': [16, 17] if gnss else []}
                elif path == '/api/resources':
                    value = {'pressure': 'normal', 'internal_free': 110000, 'internal_largest': 40000, 'psram_total': 8388608 if gnss else 0, 'web_mutations_allowed': True, 'satellites_allowed': True}
                elif path == '/api/status':
                    value = {'role': 'gateway' if gnss else 'relay-ntrip', 'degraded': False, 'rtcm_frames': 21, 'rtcm_rejected': 0, 'upstream_connected': False,
                             'services': {'ethernet': {'state': 'running'}, 'https': {'state': 'running'}}, 'espnow': {'started': False, 'configured_enabled': False}}
                elif path == '/api/gnss/status':
                    value = receiver
                elif path == '/api/gnss/capabilities':
                    value = {'model': receiver['model'], 'model_verified': receiver['model_verified'], 'fixed': receiver['model_verified']}
                elif path == '/api/gnss/satellites':
                    value = {'available': False, 'stale': True, 'satellites': []}
                elif path == '/api/ntrip/runtime':
                    value = {'upstream': {'sample_current': False, 'active': False}, 'publishers': []}
                elif path in ('/api/ntrip/history', '/api/gnss/commands', '/api/diagnostics/events'):
                    value = {'scope': 'synthetic_fixture', 'events': []}
                else:
                    raise AssertionError('Unexpected API access: ' + path)
                route.fulfill(json=value)

            context.route('**/*', route_handler)
            page.goto('https://ug.test/', wait_until='networkidle')
            page.locator('#token').fill('invalid-fixture-token')
            page.locator('#login').click()
            expect(page.locator('#notice')).to_contain_text('401')
            expect(page.locator('#session-state')).to_have_text('Session fermée')
            page.locator('#token').fill('z' * 64)
            page.locator('#login').click()
            expect(page.locator('#session-state')).to_have_text('Session ouverte')
            expect(page.locator('#card-https')).to_contain_text('running')
            for tab in ('gnss', 'rtcm', 'network', 'diagnostics', 'settings', 'dashboard'):
                page.locator('#nav-' + tab).click()
                expect(page.locator('#tab-' + tab)).to_be_visible()
                assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'), (tab, width)
            page.locator('#nav-gnss').click()
            if gnss:
                expect(page.locator('#transport')).to_be_visible()
                expect(page.locator('#base-submit')).to_be_disabled()
                page.locator('#gnss-tx').select_option('17')
                page.locator('#gnss-rx').select_option('16')
                page.locator('#transport button').click()
                expect(page.locator('#notice')).to_contain_text('saved')
                assert patches[-1]['gnss']['tx_pin'] == 17 and not patches[-1]['gnss']['enabled']
            else:
                expect(page.locator('#gnss-unavailable')).to_be_visible()
                expect(page.locator('#gnss')).not_to_be_visible()
            page.locator('#nav-rtcm').click()
            page.locator('#ntrip-password').fill('synthetic-fixture-password')
            page.locator('#ntrip-save').click()
            expect(page.locator('#ntrip-password')).to_have_value('')
            expect(page.locator('#notice')).to_contain_text('saved')
            assert patches[-1]['upstream']['endpoint']['password'] == 'synthetic-fixture-password'
            rejected[0] = True
            page.locator('#ntrip-password').fill('rejected-fixture-password')
            page.locator('#ntrip-save').click()
            expect(page.locator('#notice')).to_contain_text('400')
            expect(page.locator('#ntrip-password')).to_have_value('')
            rejected[0] = False
            page.locator('#nav-network').click()
            page.locator('#function-caster').check()
            page.locator('#functions-save').click()
            expect(page.locator('#functions-state')).to_contain_text('"caster": true')
            offline[0] = True
            page.locator('#refresh-now').click()
            expect(page.locator('#connection-state')).to_have_text('API indisponible')
            expect(page.locator('#functions-save')).to_be_disabled()
            offline[0] = False
            page.locator('#refresh-now').click()
            expect(page.locator('#connection-state')).to_have_text('API accessible')
            expect(page.locator('#functions-save')).to_be_enabled()
            page.locator('#nav-diagnostics').click()
            page.locator('#events-read').click()
            expect(page.locator('#events')).to_contain_text('synthetic_fixture')
            assert not any(p.endswith('/history') for p, _ in calls)
            page.evaluate("Object.defineProperty(document,'hidden',{configurable:true,value:true});document.dispatchEvent(new Event('visibilitychange'))")
            expect(page.locator('#freshness')).to_contain_text('pause')
            count = len(calls)
            page.wait_for_timeout(5300)
            assert len(calls) == count, 'hidden page issued API requests'
            page.evaluate("Object.defineProperty(document,'hidden',{configurable:true,value:false});document.dispatchEvent(new Event('visibilitychange'))")
            expect(page.locator('#connection-state')).to_have_text('API accessible')
            page.locator('#logout').click()
            expect(page.locator('#token')).to_have_value('')
            expect(page.locator('#session-state')).to_have_text('Session fermée')
            if artifacts:
                page.locator('#nav-dashboard').click()
                Path(artifacts).mkdir(parents=True, exist_ok=True)
                page.screenshot(path=str(Path(artifacts) / f'{"gateway" if gnss else "relay"}-{width}.png'), full_page=True)
            assert peak[0] <= 1, ('concurrent API requests', peak[0])
            assert not errors, errors
            print(f'Chromium {"gateway" if gnss else "relay"} {width}px: six tabs, no overflow, auth, forms, secrets, history, hidden pause; peak API={peak[0]} PASS')
            context.close()
        browser.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', type=Path)
    run(parser.parse_args().artifacts)
