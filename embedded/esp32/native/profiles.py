"""Bounded firmware identities. User/RF/GNSS settings are never ROM variants."""
import hashlib
import json
import re

VARIANTS = {
    's3-gateway': dict(chip='ESP32-S3',board='waveshare-s3-eth',profile='gateway',output='espnow',flash_mb=16,psram_mb=8,ethernet='w5500'),
    'c3-rover-usb': dict(chip='ESP32-C3',board='supermini-c3',profile='rover',output='usb',flash_mb=4,psram_mb=0,ethernet='none'),
    'c3-rover-uart': dict(chip='ESP32-C3',board='supermini-c3',profile='rover',output='uart',flash_mb=4,psram_mb=0,ethernet='none'),
}

def normalize(profile):
    if not isinstance(profile,dict) or type(profile.get('schema')) is not int or profile['schema']!=2 or type(profile.get('variant')) is not str or profile['variant'] not in VARIANTS:
        raise ValueError('unsupported firmware profile schema or variant')
    name=profile['variant']; expected={'schema':2,'variant':name,**VARIANTS[name]}
    if name=='c3-rover-uart':
        tx,baud=profile.get('uart_tx'),profile.get('uart_baud')
        if type(tx) is not int or tx not in (4,5,6,7,10) or type(baud) is not int or baud not in (115200,230400,460800,921600):
            raise ValueError('unsupported UART hardware option')
        expected.update(uart_tx=tx,uart_baud=baud)
    if set(profile)!=set(expected) or any(type(profile[k]) is not type(v) or profile[k]!=v for k,v in expected.items()):
        raise ValueError('unknown, runtime or incompatible firmware option')
    return expected

def build_key(source_sha, profile):
    if not isinstance(source_sha,str) or not re.fullmatch('[0-9a-f]{40}',source_sha):
        raise ValueError('source must be an identified lowercase Git commit')
    canonical=json.dumps(normalize(profile),sort_keys=True,separators=(',',':'),ensure_ascii=True)
    return hashlib.sha256((source_sha+'\n'+canonical).encode()).hexdigest()[:20]

def from_variant(variant, tx=4, baud=115200):
    profile={'schema':2,'variant':variant,**VARIANTS[variant]}
    if variant=='c3-rover-uart': profile.update(uart_tx=tx,uart_baud=baud)
    return normalize(profile)
