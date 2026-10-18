import json
import os
from pathlib import Path
import urllib.request
import urllib.parse

repo = os.environ['GH_REPOSITORY']
token = os.environ['GH_TOKEN']
headers = {'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
           'X-GitHub-Api-Version': '2022-11-28'}

def request(url, data=None, kind='application/json'):
    req = urllib.request.Request(url, data=data, headers=dict(headers, **{'Content-Type': kind}))
    with urllib.request.urlopen(req) as response:
        return json.load(response)

release = request('https://api.github.com/repos/' + repo + '/releases/tags/v0.3.2')
existing = {asset['name'] for asset in release['assets']}
for path in sorted(Path(os.environ['ASSET_DIR']).iterdir()):
    if path.name in existing:
        raise RuntimeError('Refusing to overwrite published asset: ' + path.name)
    url = ('https://uploads.github.com/repos/' + repo + '/releases/' + str(release['id']) +
           '/assets?name=' + urllib.parse.quote(path.name))
    request(url, path.read_bytes(), 'application/octet-stream')
print(release['html_url'])
