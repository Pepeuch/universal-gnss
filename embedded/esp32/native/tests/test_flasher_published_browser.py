"""Chromium integration against a verified staged site, without network or USB.

Run with playwright==1.56.0 against refresh_esp32_site.py output outside the repo.
Published firmware bytes are served locally and validated by the real WebUI JS.
"""
import argparse
import json
import mimetypes
import re
from pathlib import Path
from urllib.parse import urlparse
from playwright.sync_api import sync_playwright, expect


def run(site):
    site=Path(site).resolve(strict=True)
    catalog=json.loads((site/'flasher/catalog.json').read_text())
    approvals=json.loads((site/'flasher/approved-sources.json').read_text())['sources']
    main_sources={row['source_sha'] for row in approvals if row['publication']=='main_approved'}
    s3=next(entry for entry in catalog['images'] if entry['profile']['variant']=='s3-gateway' and entry['source_sha'] in main_sources)
    branches=json.loads((site/'flasher/branches.json').read_text()) if (site/'flasher/branches.json').exists() else {'schema':1,'branches':{}}
    if branches['branches'] and 'dev' not in branches['branches']:
        branches['branches']['dev']=next(iter(branches['branches'].values()))
    with sync_playwright() as engine:
        browser=engine.chromium.launch(headless=True)
        for failure in (200,503,404,'malformed'):
            for width in (1280,390):
                context=browser.new_context(viewport={'width':width,'height':900})
                page=context.new_page();errors=[]
                page.on('pageerror',lambda error:errors.append(str(error)))
                def route_handler(route):
                    url=urlparse(route.request.url)
                    if url.netloc=='api.github.com':
                        rows=[{'name':'main','commit':{'sha':s3['source_sha']}}]
                        rows.extend({'name':name,'commit':{'sha':'f'*40 if name.startswith('feature/') else row['source_sha']}} for name,row in branches['branches'].items())
                        rows.append({'name':'no-build-yet','commit':{'sha':'e'*40}})
                        route.fulfill(status=200,body=json.dumps(rows),content_type='application/json');return
                    if url.netloc!='ug.test':
                        route.abort();return
                    if url.path=='/flasher/branches.json':
                        route.fulfill(status=200,body=json.dumps(branches),content_type='application/json');return
                    if url.path=='/flasher/publications.json' and failure!=200:
                        route.fulfill(status=200 if failure=='malformed' else failure,body='bad json');return
                    file=(site/url.path.lstrip('/')).resolve()
                    if not file.is_relative_to(site) or not file.is_file():
                        route.fulfill(status=404,body='not found');return
                    route.fulfill(status=200,body=file.read_bytes(),content_type=mimetypes.guess_type(str(file))[0] or 'application/octet-stream')
                page.route('**/*',route_handler)
                page.goto('https://ug.test/flasher.html')
                expect(page.locator('#version option')).to_have_count(len(main_sources))
                expect(page.locator('#version')).to_have_value(s3['source_sha'])
                if failure==200:
                    expect(page.locator('#version option:checked')).to_contain_text('v0.7.5-RC1')
                expect(page.locator('#status')).to_contain_text('Firmware publié',timeout=30000)
                expect(page.locator('#image-info')).to_contain_text(s3['sha256'])
                expect(page.locator('#downloads a')).to_have_count(4)
                assert not errors,errors
                for branch,row in branches['branches'].items():
                    page.locator('#branch').select_option(branch)
                    expect(page.locator('#version')).to_have_value(row['source_sha'])
                    expect(page.locator('#status')).to_contain_text('Build de développement CI',timeout=30000)
                    expect(page.locator('#downloads a')).to_have_count(4)
                    page.locator('#check').click()
                    expect(page.locator('#branch')).to_have_value(branch)
                    expect(page.locator('#status')).to_contain_text('Build de développement CI',timeout=30000)
                    expect(page.locator('#version')).to_have_value(row['source_sha'])
                    if branch.startswith('feature/'):
                        expect(page.locator('#summary')).to_contain_text('f'*40)
                    page.locator('#consent').check()
                    expect(page.locator('#installer')).to_have_attribute('manifest',re.compile('blob:'))
                    page.locator('#branch').select_option('main')
                    expect(page.locator('#installer')).not_to_have_attribute('manifest',re.compile('.+'))
                    expect(page.locator('#consent')).not_to_be_checked()
                    expect(page.locator('#status')).to_contain_text('Firmware publié',timeout=30000)
                    # Rapid changes must never restore an obsolete branch response.
                    page.locator('#branch').select_option(branch)
                    page.locator('#branch').select_option('main')
                    expect(page.locator('#version')).to_have_value(s3['source_sha'])
                    expect(page.locator('#status')).to_contain_text('Firmware publié',timeout=30000)
                page.locator('#branch').select_option('no-build-yet')
                expect(page.locator('#status')).to_contain_text('Aucun firmware CI disponible')
                expect(page.locator('#downloads a')).to_have_count(0)
                expect(page.locator('#installer')).not_to_have_attribute('manifest',re.compile('.+'))
                assert not errors,errors
                context.close()
                print(f'Chromium {width}px, metadata {failure}: main/dev/work branch, rapid switch, missing build and verified segments PASS')
        browser.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('site',type=Path)
    run(parser.parse_args().site)
