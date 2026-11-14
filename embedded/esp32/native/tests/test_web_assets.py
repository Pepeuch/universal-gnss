"""Execute the embedded browser application against deterministic DOM/fetch mocks.

Host-only requirement: quickjs==1.19.4 (not a firmware dependency).
"""
import json
from pathlib import Path
import unittest
import quickjs

ASSETS = Path(__file__).resolve().parents[2] / 'components/ug_web/assets'


class WebAssets(unittest.TestCase):
    def test_browser_auth_configuration_and_base_confirmation(self):
        vm = quickjs.Context()
        vm.set_memory_limit(8 * 1024 * 1024)
        vm.set_time_limit(2)
        vm.eval(r'''
          const elements = {}, calls = [];
          const document = {getElementById(id) {return elements[id] ||= {value:'', textContent:'', hidden:false,children:[],appendChild(child){this.children.push(child);},replaceChildren(){this.children=[];}};},
                            createElement() {return {click(){},children:[],appendChild(child){this.children.push(child);}};}};
          const setInterval = () => {};
          const setTimeout = () => 1, clearTimeout = () => {};
          class AbortController {constructor(){this.signal={};}abort(){}}
          let hold_selftest = false, release_selftest;
          let hold_commands = false, release_commands;
          let hold_status = false, release_status, pressure = 'normal', hold_events = false, release_events;
          const confirm = () => true;
          const prompt = text => text.includes('REPLACE_TLS_OBJECT') ? 'REPLACE_TLS_OBJECT' : text.includes('DELETE_TLS_OBJECT') ? 'DELETE_TLS_OBJECT' : text.includes('ROLLBACK_CONFIGURATION') ? 'ROLLBACK_CONFIGURATION' : 'SAVE_RECEIVER_CONFIGURATION';
          class FormData {constructor(value){this.value=value;} get(key){return this.value[key] ?? '';}
                          has(key){return Boolean(this.value[key]);}}
          const fetch = async (path, options) => {
            calls.push({path,options});
            if (hold_status && path === '/api/gnss/status') return new Promise(resolve => {
              release_status = () => resolve({ok:true,headers:{get:()=> 'application/json'},text:async()=>'{"private":"must-not-reappear"}'});
            });
            if (path === '/api/diagnostics/wire-selftest') {
              if (hold_selftest) return new Promise(resolve => {
                release_selftest = () => resolve({ok:true,headers:{get:()=> 'application/json'},text:async()=>'{"private":"old-selftest-session"}'});
              });
              return {ok:true,headers:{get:()=> 'application/json'},text:async()=>'{"ok":true,"scope":"offline_private_codec_buffers","checks":{"crc_vectors":true}}'};
            }
            if (path === '/api/gnss/commands') {
              if (hold_commands) return new Promise(resolve => {
                release_commands = () => resolve({ok:true,headers:{get:()=> 'application/json'},text:async()=>'{"private":"old-command-session"}'});
              });
              return {ok:true,headers:{get:()=> 'application/json'},text:async()=>'{"scope":"volatile_gnss_command_results_this_boot","events":[{"purpose":"base_disable","indeterminate":true}]}'};
            }
            if (hold_events && path === '/api/diagnostics/events') return new Promise(resolve => {
              release_events = () => resolve({ok:true,headers:{get:()=> 'application/json'},text:async()=>'{"events":[{"private":"old-session"}]}'});
            });
            const payload = path === '/api/ntrip/runtime' ? {configuration_revision:'7',upstream:{sample_current:false,active:false}} : path === '/api/diagnostics/events' ? {scope:'volatile_service_events_this_boot',events:[{sequence:'3',service:'rtcm_router',kind:'watchdog',error:7}]} : path === '/api/resources' ? {pressure,satellites_allowed:pressure === 'normal',web_mutations_allowed:pressure !== 'critical'} : path === '/api/config/history' ? {generation:'3',previous_generation:'2',previous_available:true,writable:true} : path === '/api/gnss/capabilities' ? {model:'ZED-F9P',model_verified:true,fixed:true,survey_in:true,time_optimization:false,telemetry:true,disable_base:true} : path === '/api/gnss/satellites' ? {available:true,source:'NMEA-GSV',stale:false,satellites:[{svid:2,cn0:45.5}]} : path === '/api/capabilities' ? {gnss:true,gnss_allowed_pins:[17,18]} : path === '/api/config' ?
              {schema:1,input:'ntrip',gnss:{enabled:false,tx_pin:-1,rx_pin:-1,baud:115200,model:'auto'},upstream:{endpoint:{password_set:true}}} : {};
            return {ok:true,status:200,headers:{get:()=> 'application/json'},text:async()=>JSON.stringify(payload)};
          };
        ''')
        vm.eval((ASSETS / 'app.js').read_text())
        self.drain(vm)
        vm.eval("document.getElementById('token').value='z'.repeat(64); elements.login.onclick();")
        self.drain(vm)
        calls = json.loads(vm.eval('JSON.stringify(calls)'))
        self.assertTrue(any(c['options']['headers'].get('Authorization') == 'Bearer ' + 'z'*64 for c in calls))
        self.assertFalse(vm.eval("elements['base-submit'].disabled"))
        self.assertFalse(vm.eval("elements['base-disable'].disabled"))
        vm.eval("document.getElementById('base-disable-port').value='2'; document.getElementById('base-disable-persist').checked=true; elements['base-disable'].onclick();")
        self.drain(vm)
        disable = json.loads(vm.eval("calls.filter(c=>c.path==='/api/gnss/base/disable').pop().options.body"))
        self.assertEqual(disable, {'confirm':'DISABLE_BASE_MODE','receiver_port':2,'persist':True,'persistent_confirmation':'SAVE_RECEIVER_CONFIGURATION'})
        vm.eval("receiverPlans.disable_base=false; elements['base-disable'].onclick();")
        self.drain(vm)
        self.assertEqual(vm.eval("calls.filter(c=>c.path==='/api/gnss/base/disable').length"), 1)
        vm.eval("receiverPlans.disable_base=true;")
        vm.eval('elements.detect.onclick();')
        self.drain(vm)
        detect = json.loads(vm.eval("calls.filter(c=>c.path==='/api/gnss/detect').pop().options.body"))
        self.assertEqual(detect['confirm'], 'DETECT_RECEIVER')
        self.assertEqual(vm.eval('elements.token.value'), '')
        vm.eval("elements.base.onsubmit({preventDefault(){},target:{mode:'fixed',height_datum:'ellipsoidal',latitude_deg:'48.123456789',longitude_deg:'2.123456789',height_m:'123.4567',persist:true}});")
        self.drain(vm)
        operation = json.loads(vm.eval('calls[calls.length-1].options.body'))
        self.assertEqual(operation['latitude_deg'], 48.123456789)
        self.assertEqual(operation['confirm'], 'APPLY_BASE_CONFIGURATION')
        self.assertEqual(operation['persistent_confirmation'], 'SAVE_RECEIVER_CONFIGURATION')
        vm.eval("elements['gnss-enabled'].checked=true; elements['gnss-tx'].value='17'; elements['gnss-rx'].value='18'; elements['correction-input'].value='local'; elements.transport.onsubmit();")
        self.drain(vm)
        patch = json.loads(vm.eval("calls.filter(c=>c.path==='/api/config' && c.options.method==='POST').pop().options.body"))
        self.assertEqual(patch['gnss']['tx_pin'], 17)
        self.assertEqual(patch['gnss']['rx_pin'], 18)
        self.assertTrue(patch['gnss']['enabled'])
        self.assertEqual(patch['input'], 'local')
        vm.eval("document.getElementById('tls-name').value='ca'; document.getElementById('tls-kind').value='certificate'; document.getElementById('tls-pem').value='public-test-data'; elements['tls-object'].onsubmit();")
        self.drain(vm)
        tls = json.loads(vm.eval("calls.filter(c=>c.path==='/api/tls/object').pop().options.body"))
        self.assertEqual(tls['confirm'], 'REPLACE_TLS_OBJECT')
        self.assertEqual(tls['kind'], 'certificate')
        self.assertEqual(vm.eval("elements['tls-pem'].value"), '')
        vm.eval("elements['tls-kind'].value='delete'; elements['tls-object'].onsubmit();")
        self.drain(vm)
        tls = json.loads(vm.eval("calls.filter(c=>c.path==='/api/tls/object').pop().options.body"))
        self.assertEqual(tls['confirm'], 'DELETE_TLS_OBJECT')
        self.assertNotIn('pem', tls)
        vm.eval("document.getElementById('telemetry-rate').value='5'; document.getElementById('telemetry-persist').checked=true; elements.telemetry.onsubmit();")
        self.drain(vm)
        telemetry = json.loads(vm.eval("calls.filter(c=>c.path==='/api/gnss/telemetry/apply').pop().options.body"))
        self.assertEqual(telemetry['rate_hz'], 5)
        self.assertEqual(telemetry['confirm'], 'APPLY_GNSS_TELEMETRY')
        self.assertEqual(telemetry['persistent_confirmation'], 'SAVE_RECEIVER_CONFIGURATION')
        vm.eval("elements['ntrip-read'].onclick();")
        self.drain(vm)
        self.assertIn('sample_current', vm.eval("elements['ntrip-runtime'].textContent"))
        vm.eval("elements['ntrip-restart'].onclick();")
        self.drain(vm)
        restart = json.loads(vm.eval("calls.filter(c=>c.path==='/api/ntrip/restart').pop().options.body"))
        self.assertEqual(restart, {'confirm':'RESTART_NTRIP_SERVICES'})
        vm.eval("elements['wire-selftest'].onclick();")
        self.drain(vm)
        self.assertIn('offline_private_codec_buffers', vm.eval("elements['wire-selftest-result'].textContent"))
        test_body = json.loads(vm.eval("calls.filter(c=>c.path==='/api/diagnostics/wire-selftest').pop().options.body"))
        self.assertEqual(test_body, {'confirm':'RUN_OFFLINE_WIRE_SELFTEST'})
        vm.eval("elements['commands-read'].onclick();")
        self.drain(vm)
        self.assertIn('base_disable', vm.eval("elements['command-history'].textContent"))
        self.assertIn('indeterminate', vm.eval("elements['command-history'].textContent"))
        vm.eval("elements['events-read'].onclick();")
        self.drain(vm)
        self.assertIn('rtcm_router', vm.eval('elements.events.textContent'))
        vm.eval('elements.rollback.onclick();')
        self.drain(vm)
        rollback = json.loads(vm.eval("calls.filter(c=>c.path==='/api/config/rollback').pop().options.body"))
        self.assertEqual(rollback, {'confirm':'ROLLBACK_CONFIGURATION', 'expected_generation':'3'})
        self.assertEqual(vm.eval("elements['satellite-rows'].children.length"), 1)
        vm.eval("pressure='reduced'; calls.length=0; refresh();")
        self.drain(vm)
        self.assertTrue(vm.eval("calls.some(c=>c.path==='/api/status')"))
        self.assertFalse(vm.eval("calls.some(c=>c.path==='/api/gnss/satellites')"))
        self.assertTrue(vm.eval("elements['wire-selftest'].disabled"))
        vm.eval("elements['wire-selftest'].onclick();")
        self.drain(vm)
        self.assertFalse(vm.eval("calls.some(c=>c.path==='/api/diagnostics/wire-selftest')"))
        vm.eval("pressure='critical'; calls.length=0; refresh();")
        self.drain(vm)
        self.assertEqual(vm.eval('calls.length'), 1)
        self.assertEqual(vm.eval('calls[0].path'), '/api/resources')
        self.assertIn('Pression mémoire', vm.eval('elements.notice.textContent'))
        self.assertTrue(vm.eval("elements['base-submit'].disabled"))
        self.assertEqual(vm.eval("elements['satellite-rows'].children.length"), 0)
        vm.eval("pressure='normal'; refresh();")
        self.drain(vm)
        vm.eval('hold_status=true; refresh();')
        self.drain(vm)
        before = vm.eval('calls.length')
        vm.eval('refresh(); refresh();')
        self.drain(vm)
        self.assertEqual(vm.eval('calls.length'), before)
        vm.eval("hold_events=true; elements['events-read'].onclick();")
        self.drain(vm)
        vm.eval("hold_commands=true; elements['commands-read'].onclick();")
        self.drain(vm)
        vm.eval("hold_selftest=true; elements['wire-selftest'].onclick();")
        self.drain(vm)
        count = vm.eval("calls.filter(c=>c.path==='/api/diagnostics/wire-selftest').length")
        vm.eval("elements['wire-selftest'].onclick();")
        self.drain(vm)
        self.assertEqual(vm.eval("calls.filter(c=>c.path==='/api/diagnostics/wire-selftest').length"), count)
        vm.eval('elements.logout.onclick(); release_status(); release_events(); release_commands(); release_selftest();')
        self.drain(vm)
        self.assertEqual(vm.eval('elements.receiver.textContent'), 'Session fermée')
        self.assertEqual(vm.eval('elements.events.textContent'), 'Session fermée')
        self.assertEqual(vm.eval("elements['command-history'].textContent"), 'Session fermée')
        self.assertEqual(vm.eval("elements['wire-selftest-result'].textContent"), 'Session fermée')
        self.assertTrue(vm.eval("elements['wire-selftest'].disabled"))
        self.assertEqual(vm.eval("elements['ntrip-runtime'].textContent"), 'Session fermée')
        self.assertEqual(vm.eval("elements['satellite-rows'].children.length"), 0)
        self.assertEqual(vm.eval('elements.config.value'), '')
        self.assertEqual(vm.eval('token'), '')
        self.assertTrue(vm.eval("elements['base-submit'].disabled"))
        self.assertTrue(vm.eval("elements['telemetry-submit'].disabled"))
        self.assertTrue(vm.eval("elements['base-disable'].disabled"))
        self.assertNotIn('localStorage', (ASSETS/'app.js').read_text())
        self.assertNotIn('innerHTML', (ASSETS/'app.js').read_text())
        html = (ASSETS/'index.html').read_text()
        self.assertIn('src="/app.js"', html)
        self.assertNotIn('<script>', html)

    @staticmethod
    def drain(vm):
        for _ in range(100):
            if not vm.execute_pending_job():
                return
        raise AssertionError('browser promise budget exceeded')


if __name__ == '__main__':
    unittest.main()
