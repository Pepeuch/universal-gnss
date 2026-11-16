#!/usr/bin/env python3
"""Retain validated durable firmware history; never approve a source here."""
import datetime
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('history_refresh',ROOT/'tools/branches/refresh_esp32_site.py')
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)


def merge(site):
    site=Path(site)
    data,tag=w.durable_archive()  # No empty fallback: an invalid source blocks publishing.
    with tempfile.TemporaryDirectory(prefix='ug-history-') as temp:
        old=w.refresh(data,tag,Path(temp)/'verified')
        current=w.a.s.p.load(site/'flasher/catalog.json')
        previous=w.a.s.p.load(old/'flasher/catalog.json')
        approvals=w.a.s.p.load(site/'flasher/approved-sources.json')
        previous_approvals=w.a.s.p.load(old/'flasher/approved-sources.json')
        by_sha={r['source_sha']:r for r in previous_approvals['sources']}
        for row in approvals['sources']:
            if row['source_sha'] in by_sha and by_sha[row['source_sha']]['source_tree']!=row['source_tree']:
                raise ValueError('historical approval conflict')
            by_sha[row['source_sha']]=row
        rows={e['build_key']:e for e in previous['images']}
        for entry in current['images']:
            key=entry['build_key']
            if key in rows and rows[key]!=entry:raise ValueError('immutable firmware identity conflict')
            rows[key]=entry
        for directory in (old/'flasher/published').iterdir():
            dest=site/'flasher/published'/directory.name
            if dest.exists():
                before={f.name:f.read_bytes() for f in directory.iterdir()}
                after={f.name:f.read_bytes() for f in dest.iterdir()}
                if before!=after:raise ValueError('immutable firmware bytes conflict')
            else:shutil.copytree(directory,dest)
        if sum(p.stat().st_size for p in (site/'flasher/published').rglob('*') if p.is_file())>512*1024*1024:
            raise ValueError('catalogue mirror exceeds bounded storage; do not truncate history')
        if len(rows)>512 or len(by_sha)>128:raise ValueError('catalogue capacity exceeded; do not truncate history')
        approvals={'schema':1,'sources':list(by_sha.values())}
        w.a.s.r.approval_register(approvals)
        old_meta=w.a.s.p.load(old/'alpha-publication.json')
        history_path=old/'flasher/publications.json'
        if history_path.exists():
            publications=w.a.s.p.load(history_path)['publications']
        else:
            import urllib.request
            with urllib.request.urlopen('https://api.github.com/repos/'+w.a.REPO+'/releases/tags/'+tag,timeout=30) as response:
                published=json.load(response)['published_at']
            publications=[{'source_sha':old_meta['source_sha'],'published_at':published,'status':'validated_authorized_alpha','release_tag':tag}]
        meta=w.a.s.p.load(site/'alpha-publication.json')
        if not any(row['source_sha']==meta['source_sha'] for row in publications):
            publications.append({'source_sha':meta['source_sha'],'published_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),
                                 'status':'validated_authorized_alpha','release_tag':meta['release_tag']})
        publications.sort(key=lambda row:datetime.datetime.fromisoformat(row['published_at'].replace('Z','+00:00')).timestamp(),reverse=True)
        order={row['source_sha']:i for i,row in enumerate(publications)}
        images=sorted(rows.values(),key=lambda row:(order.get(row['source_sha'],999),row['build_key']))
        (site/'flasher/catalog.json').write_bytes(w.a.s.p.json_bytes({'schema':2,'images':images}))
        (site/'flasher/approved-sources.json').write_bytes(w.a.s.p.json_bytes(approvals))
        (site/'flasher/publications.json').write_bytes(w.a.s.p.json_bytes({'schema':1,'publications':publications}))
