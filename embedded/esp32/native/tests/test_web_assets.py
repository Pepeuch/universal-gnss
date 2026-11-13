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
          const document = {getElementById(id) {return elements[id] ||= {value:'', textContent:'', hidden:false,appendChild(){}};},
                            createElement() {return {click(){}};}};
          const setInterval = () => {};
          const confirm = () => true;
          const prompt = () => 'SAVE_RECEIVER_CONFIGURATION';
          class FormData {constructor(value){this.value=value;} get(key){return this.value[key] ?? '';}
                          has(key){return Boolean(this.value[key]);}}
          const fetch = async (path, options) => {
            calls.push({path,options});
            const payload = path === '/api/capabilities' ? {gnss:true,gnss_allowed_pins:[17,18]} : path === '/api/config' ?
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
        vm.eval('elements.logout.onclick();')
        self.assertEqual(vm.eval('elements.config.value'), '')
        self.assertEqual(vm.eval('token'), '')
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
