"""Chromium integration against a verified staged site, without network or USB.

Run with playwright==1.56.0 against refresh_esp32_site.py output outside the repo.
Published firmware bytes are served locally and validated by the real WebUI JS.
"""
import argparse
import json
import mimetypes
from pathlib import Path
from urllib.parse import urlparse
from playwright.sync_api import sync_playwright, expect


def run(site):
    site=Path(site).resolve(strict=True)
    catalog=json.loads((site/'flasher/catalog.json').read_text())
    s3=next(entry for entry in catalog['images'] if entry['profile']['variant']=='s3-gateway')
    with sync_playwright() as engine:
        browser=engine.chromium.launch(headless=True)
        for failure in (503,404,'malformed'):
            for width in (1280,390):
                context=browser.new_context(viewport={'width':width,'height':900})
                page=context.new_page();errors=[]
                page.on('pageerror',lambda error:errors.append(str(error)))
                def route_handler(route):
                    url=urlparse(route.request.url)
                    if url.netloc!='ug.test':
                        route.abort();return
                    if url.path=='/flasher/publications.json':
                        route.fulfill(status=200 if failure=='malformed' else failure,body='bad json');return
                    file=(site/url.path.lstrip('/')).resolve()
                    if not file.is_relative_to(site) or not file.is_file():
                        route.fulfill(status=404,body='not found');return
                    route.fulfill(status=200,body=file.read_bytes(),content_type=mimetypes.guess_type(str(file))[0] or 'application/octet-stream')
                page.route('**/*',route_handler)
                page.goto('https://ug.test/flasher.html')
                expect(page.locator('#version option')).to_have_count(len({entry['source_sha'] for entry in catalog['images']}))
                expect(page.locator('#version')).to_have_value(s3['source_sha'])
                expect(page.locator('#status')).to_contain_text('Firmware publié',timeout=30000)
                expect(page.locator('#image-info')).to_contain_text(s3['sha256'])
                expect(page.locator('#downloads a')).to_have_count(4)
                assert not errors,errors
                context.close()
                print(f'Chromium {width}px, metadata {failure}: published version and all four verified segments PASS')
        browser.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('site',type=Path)
    run(parser.parse_args().site)
