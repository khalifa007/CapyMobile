"""Puts the built Capy Mobile on a PS5 over FTP, where ShadowMount+ mounts it from /data/homebrew.

    python deploy.py <host>                             the app (dist/<title ID>, built by build.sh)
    python deploy.py <host> --games <folder or .jar> [...]    only games, into the app's games folder
    python deploy.py <host> --log                       only prints the app's log

host    the PS5's address (or set PS5_HOST); its FTP server on 2121 (ftpsrv), else on 1337 (etaHEN's).

The app's folder also holds your games and saves, so nothing on the console is deleted and only the
app's own files are replaced. eboot.bin and sce_sys/param.json go last, so ShadowMount+ never
registers half an app. It scans only while no game is running: close the running game or app, and the
icon appears within a minute.
"""
import ftplib
import io
import json
import os
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
TITLE = json.loads((HERE / 'sce_sys' / 'param.json').read_text(encoding='utf-8'))['titleId']
TARGET = '/data/homebrew/' + TITLE
LAST = ['eboot.bin', 'sce_sys/param.json']


def connect(host):
    for port in (2121, 1337):
        try:
            ftp = ftplib.FTP()
            ftp.connect(host, port, timeout=15)
            ftp.login()
            return ftp
        except OSError:
            continue
    sys.exit(f'No FTP server answers on {host} (2121, 1337). Start one on the PS5 first.')


def read(ftp, path):
    data = io.BytesIO()
    try:
        ftp.retrbinary('RETR ' + path, data.write)
    except ftplib.all_errors:
        return None
    return data.getvalue()


def make_folders(ftp, path, made):
    folder = ''
    for part in path.strip('/').split('/'):
        folder += '/' + part
        if folder not in made and folder.startswith(TARGET):
            try:
                ftp.sendcmd('MKD ' + folder)   # ftpsrv answers 226, which ftplib's mkd() refuses
            except ftplib.error_perm:
                pass
            made.add(folder)


def send(ftp, source, path):
    # Under another name first, then renamed over the old file: the app may be running, and a file
    # that is being replaced is never seen half written.
    with open(source, 'rb') as stream:
        ftp.storbinary('STOR ' + path + '.capy-new', stream)
    ftp.sendcmd('RNFR ' + path + '.capy-new')
    ftp.sendcmd('RNTO ' + path)
    print(f'sent {path} ({source.stat().st_size} bytes)')


def push(ftp):
    source = HERE / 'dist' / TITLE
    if not (source / 'eboot.bin').is_file():
        sys.exit(f'{source} is not built; run build.sh first.')
    files = sorted(p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file())
    files.sort(key=lambda name: LAST.index(name) if name in LAST else -1)
    made = set()
    for folder in ('games', 'saves'):
        make_folders(ftp, f'{TARGET}/{folder}', made)
    for name in files:
        make_folders(ftp, f'{TARGET}/{name}'.rsplit('/', 1)[0], made)
        send(ftp, source / name, f'{TARGET}/{name}')
    print(f'{TITLE} is in {TARGET}.')


def push_games(ftp, sources):
    jars = []
    for source in map(pathlib.Path, sources):
        jars += sorted(source.glob('*.jar')) if source.is_dir() else [source]
    if not jars:
        sys.exit('No .jar files there.')
    make_folders(ftp, TARGET + '/games', set())
    for jar in jars:
        send(ftp, jar, f'{TARGET}/games/{jar.name}')
    print(f'{len(jars)} game(s) sent. In Capy Mobile, press triangle to look for new games.')


def show_log(ftp):
    raw = read(ftp, TARGET + '/capy-mobile.log')
    print('(no log there)' if raw is None else raw.decode('utf-8', 'replace'))


def main():
    args = sys.argv[1:]
    games = args[args.index('--games') + 1:] if '--games' in args else None
    before = args[:args.index('--games')] if games is not None else args
    hosts = [a for a in before if not a.startswith('--')]
    host = hosts[0] if hosts else os.environ.get('PS5_HOST')
    if not host:
        sys.exit(__doc__)
    ftp = connect(host)
    print(ftp.getwelcome().splitlines()[0])
    if '--log' in before:
        show_log(ftp)
    elif games is not None:
        push_games(ftp, games)
    else:
        push(ftp)
    ftp.quit()


if __name__ == '__main__':
    main()
