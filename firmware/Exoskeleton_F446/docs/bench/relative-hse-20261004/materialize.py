"""Materialize an archived source profile only; never build, flash or contact devices."""
from pathlib import Path
import argparse,hashlib,json
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,help='New directory outside this firmware checkout')
    args=parser.parse_args()
    profile=Path(__file__).resolve().parent
    firmware=profile.parents[2]
    output=Path(args.output).resolve()
    if output==firmware or firmware in output.parents or output.exists():
        parser.error('Output must be a new separate directory; never overwrite the checkout')
    manifest=json.loads((profile/'manifest.json').read_text(encoding='utf-8'))
    files=[]
    for item in manifest['inputs']:
        source=profile/item['overlay'] if 'overlay' in item else firmware/item['path']
        data=source.read_bytes().replace(b'\r\n',b'\n')
        if hashlib.sha256(data).hexdigest()!=item['sha256_lf']:
            raise SystemExit('Source differs from accepted archive: '+item['path'])
        files.append((item['path'],data))
    output.mkdir(parents=True)
    for relative,data in files:
        target=output/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    print('Materialized '+str(len(files))+' verified input files. No build,flash,serial orCAN operation.')
if __name__=='__main__':main()
