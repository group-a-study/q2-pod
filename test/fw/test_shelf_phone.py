"""patch/shelf_phone.c and patch/qr.c: Add an App, the App Store's page on the phone. Its QR codes against the card
apps' encoder, the links and forms it reads, signing a phone in, adding and removing repos (the card, GitHub and the
Wi-Fi stubbed), and the App Store's list with them (netupdate.c)."""
import ctypes as C
import html
import json
import pathlib

import pytest

from test_netupdate import Card, Net, Page, Sh, App, SH_MAX, ROW_ADD, apps, github

PH_OUT = 32768 + 512
MINE = b'/mnt/mmc/.q2/my-sources.list'
LIST = b'/mnt/mmc/.q2/sources.list'
LOOKUP = b'/tmp/q2-shelf-phone.json'
STATE = b'/tmp/q2-apps.state'
KEY, PIN = b'0123456789abcdef', b'548001'
VECTORS = json.loads((pathlib.Path(__file__).parent/'qr_vectors.json').read_text())


class Ph(C.Structure):
    _fields_ = [('thread', C.c_ulong), *((n, C.c_int) for n in ('running', 'stop', 'seq')), ('xfer', C.c_int * 2),
                ('used', C.c_uint), *((n, C.c_int) for n in ('fd', 'port', 'tries', 'seen')), ('timer', C.c_uint),
                ('key', C.c_char * 17), ('pin', C.c_char * 7), ('ip', C.c_char * 16), ('note', C.c_char * 96),
                ('said', C.c_char * 320)]


def qr(fw, data, mask=-1, room=640):
    hex_ = C.create_string_buffer(room)
    size = fw.fn('qr_hex', C.c_int, C.c_char_p, C.c_uint, C.c_int, C.c_void_p, C.c_uint)(data, len(data), mask, hex_, room)
    return size, hex_.value.decode()


@pytest.mark.parametrize('v', VECTORS, ids=lambda v: f'{len(v["data"].encode())}b-mask{v["mask"]}')
def test_codes_match_the_card_apps_encoder(fw, v):
    """internal/qr's (Go, the card apps' own, which phones read), from version 1 to 8, each mask, and its choice."""
    assert qr(fw, v['data'].encode(), v['mask']) == (v['size'], v['hex'])


def test_a_code_that_cant_be_drawn_is_none(fw):
    assert qr(fw, b'w' * 152)[0] == 49 and qr(fw, b'w' * 153) == (0, '')  # version 8 is the most the QR page draws
    assert qr(fw, b'a', room=21 * 6) == (0, '') and qr(fw, b'a', room=21 * 6 + 1)[0] == 21  # the hex and its NUL


def repo_of(fw, s):
    out = C.create_string_buffer(80)
    return out.value if fw.fn('shelf_phone__repo_of', C.c_int, C.c_char_p, C.c_void_p)(s, out) else None


@pytest.mark.parametrize('pasted,repo', [
    (b'https://github.com/group-a-study/q2-bandcamp', b'group-a-study/q2-bandcamp'),
    (b'  https://GitHub.com/Group-A-Study/Q2-Bandcamp.git', b'group-a-study/q2-bandcamp'),
    (b'github.com/me/q2-foo/releases/latest', b'me/q2-foo'),
    (b'http://www.github.com/me/q2-foo?tab=readme#install', b'me/q2-foo'),
    (b'me/q2-foo', b'me/q2-foo'), (b'me/q2_f.oo', b'me/q2_f.oo'), (b'me/.git', b'me/.git'),
    (b'a' * 39 + b'/' + b'b' * 39, b'a' * 39 + b'/' + b'b' * 39),
    (b'', None), (b'q2-foo', None), (b'/q2-foo', None), (b'me/', None), (b'm.e/foo', None), (b'me/f o', b'me/f'),
    (b'a' * 40 + b'/foo', None), (b'me/f%oo', None), (b'a' * 39 + b'/' + b'b' * 40, None),
    (b'https://gitlab.com/me/q2-foo', None),
])
def test_what_the_phone_pastes_becomes_owner_slash_repo(fw, pasted, repo):
    assert repo_of(fw, pasted) == repo


def form_field(fw, form, key, n=64):
    out = C.create_string_buffer(n)
    there = fw.fn('shelf_phone__form_field', C.c_int, C.c_char_p, C.c_char_p, C.c_void_p, C.c_uint)(form, key, out, n)
    return out.value if there else None


def test_a_forms_fields_decoded(fw):
    form = b'repo=https%3A%2F%2Fgithub.com%2Fme%2Fq2-foo&name=My+App+%C3%A9&pin=548+001&x=%zz%4'
    assert form_field(fw, form, b'repo') == b'https://github.com/me/q2-foo'
    assert form_field(fw, form, b'name') == 'My App é'.encode()
    assert form_field(fw, form, b'pin') == b'548 001' and form_field(fw, form, b'x') == b'%zz%4'
    assert form_field(fw, form, b'nam') is None and form_field(fw, b'', b'repo') is None
    assert form_field(fw, form, b'repo', 9) == b'https://'  # cut to its buffer


def name_of(fw, repo):
    out = C.create_string_buffer(32)
    fw.fn('shelf_phone__name_of', None, C.c_char_p, C.c_void_p, C.c_uint)(repo, out, 32)
    return out.value


def name_clean(fw, s):
    out = C.create_string_buffer(s, 96)
    fw.fn('shelf_phone__name_clean', None, C.c_void_p)(out)
    return out.value


def test_names(fw):
    assert name_of(fw, b'me/q2-file-browser') == b'File browser' and name_of(fw, b'me/q2-') == b'Q2 '.strip()
    assert name_of(fw, b'me/bandcamp_dl') == b'Bandcamp dl' and name_of(fw, b'me/' + b'x' * 40) == b'X' + b'x' * 30
    assert name_clean(fw, b'  Two\tlines\nhere  ') == b'Two lines here'
    long = 'é'.encode() * 16  # 32 bytes: the 16th character would split at 31
    assert name_clean(fw, long) == 'é'.encode() * 15 and name_clean(fw, b'a' * 30 + b' b') == b'a' * 30


class Phone:
    """The page's server state, a phone's requests answered as talk() hands them over, on a Card with GitHub stubbed."""
    def __init__(self, fw, card_files=None, releases=None):
        self.fw = fw
        self.card = Card(fw, card_files)
        self.net = Net(fw, releases or {b'https://api.github.com/repos/': (0, 404, None)})
        self.ph = fw.var('shelf_phone__ph', Ph)
        self.ph.key, self.ph.pin, self.ph.port = KEY, PIN, 8300
        self.answer = fw.fn('shelf_phone__answer', C.c_uint, C.c_char_p, C.c_char_p, C.c_void_p, C.c_uint)

    def ask(self, line, body=b'', cookie=KEY):
        head = line + b' HTTP/1.1\r\nHost: q2\r\n' + (b'Cookie: theme=x; q2apps=' + cookie + b'\r\n' if cookie else b'')
        out = C.create_string_buffer(PH_OUT)
        n = self.answer(head, body, out, PH_OUT)
        top, _, page = out.raw[:n].partition(b'\r\n\r\n')
        status, *fields = top.split(b'\r\n')
        headers = dict(f.split(b': ', 1) for f in fields)
        assert int(headers[b'Content-Length']) == len(page) and headers[b'Connection'] == b'close'
        return int(status.split()[1]), headers, html.unescape(page.decode())

    def release(self, repo, tag=b'q2-v1.1', *assets):
        """GitHub's latest release of repo, read through shelf_lookup's file."""
        self.net.replies = {b'https://api.github.com/repos/' + repo: (0, 200, None), **self.net.replies}
        self.card.files[LOOKUP] = github(tag.decode(), *(assets or [('q2-x.zip', 3 << 20)]))


def test_a_phone_without_the_key_is_asked_the_pin(fw):
    p = Phone(fw)
    code, headers, page = p.ask(b'GET /', cookie=None)
    assert code == 200 and 'Type the PIN the Q2 shows' in page and 'action="/pin"' in page and b'Set-Cookie' not in headers
    assert p.ask(b'GET /', cookie=KEY + b'0')[2] == page and p.ask(b'GET /', cookie=KEY[:-1])[2] == page
    assert p.ask(b'GET /?k=' + KEY[:-1] + b'0', cookie=None)[2] == page
    assert p.ask(b'POST /add', b'repo=me/foo', cookie=None)[0] == 303 and MINE not in p.card.files  # nothing done
    assert p.ask(b'GET /x', cookie=None)[0] == 303


def test_the_qr_codes_key_signs_the_phone_in_and_leaves_the_address_bar(fw):
    p = Phone(fw)
    code, headers, _ = p.ask(b'GET /?k=' + KEY, cookie=None)
    assert code == 303 and headers[b'Location'] == b'/'
    assert headers[b'Set-Cookie'] == b'q2apps=' + KEY + b'; Path=/; HttpOnly; SameSite=Lax'
    assert headers[b'Referrer-Policy'] == b'no-referrer' and headers[b'Cache-Control'] == b'no-store'
    code, _, page = p.ask(b'GET /')
    assert code == 200 and 'action="/add"' in page and 'None yet' in page


def test_the_pin_signs_in_but_five_wrong_ones_shut_the_page(fw):
    p = Phone(fw)
    code, headers, _ = p.ask(b'POST /pin', b'pin=548+001', cookie=None)  # as the Q2 shows it, spaced
    assert code == 303 and headers[b'Set-Cookie'].startswith(b'q2apps=' + KEY)
    for k in range(4):
        code, headers, page = p.ask(b'POST /pin', b'pin=000000', cookie=None)
        assert code == 200 and "That isn't the PIN the Q2 shows" in page and b'Set-Cookie' not in headers
    assert 'Too many wrong PINs' in p.ask(b'POST /pin', b'pin=000000', cookie=None)[2]
    code, headers, page = p.ask(b'POST /pin', b'pin=548001', cookie=None)
    assert code == 200 and b'Set-Cookie' not in headers and p.ph.tries == 6
    assert p.ask(b'GET /?k=' + KEY, cookie=None)[0] == 303  # the QR code's key still signs in


def test_other_requests(fw):
    p = Phone(fw)
    assert p.ask(b'GET /favicon.ico')[0] == 404 and p.ask(b'PUT /')[0] == 404 and p.ask(b'GET /add')[0] == 404


def test_a_repo_is_added_once_github_shows_a_zip(fw):
    p = Phone(fw, {LIST: b'DiamondBond/q2-rockbox Rockbox\nme/q2-foo Foo\n'})
    p.release(b'group-a-study/q2-bandcamp', b'q2-v1.1', ('notes.txt', 3), ('q2-bandcamp-v1.1.zip', 2516582))
    code, headers, _ = p.ask(b'POST /add', b'repo=https%3A%2F%2Fgithub.com%2Fgroup-a-study%2Fq2-bandcamp&name=')
    assert code == 303 and headers[b'Location'] == b'/'
    assert p.card.files[MINE] == b'group-a-study/q2-bandcamp Bandcamp\n' and p.ph.seq == 1
    assert p.net.urls == [b'https://api.github.com/repos/group-a-study/q2-bandcamp/releases/latest']
    assert p.ph.note == b'Bandcamp added: press Return, then its row' and b'note=Bandcamp added' in p.card.files[STATE]
    page = p.ask(b'GET /')[2]
    assert 'Added Bandcamp (q2-v1.1, 2.4 MB). On the Q2, press Return: Bandcamp has a row' in page
    assert '<b>Bandcamp</b><a href="https://github.com/group-a-study/q2-bandcamp">' in page and '>Remove</button>' in page
    assert '<input type="hidden" name="repo" value="group-a-study/q2-bandcamp">' in page
    assert '<h2>In the App Store</h2><p class="all">Rockbox, Foo</p>' in page
    assert 'Added Bandcamp' not in p.ask(b'GET /')[2]  # said once
    p.release(b'me/q2-bar')
    assert p.ask(b'POST /add', b'repo=me/q2-bar&name=+My+%3Cb%3EBar%3C/b%3E+')[0] == 303
    assert p.card.files[MINE] == b'group-a-study/q2-bandcamp Bandcamp\nme/q2-bar My <b>Bar</b>\n' and p.ph.seq == 2
    out = C.create_string_buffer(PH_OUT)
    n = p.answer(b'GET / HTTP/1.1\r\nCookie: q2apps=' + KEY + b'\r\n', b'', out, PH_OUT)
    assert b'<b>My &lt;b&gt;Bar&lt;/b&gt;</b>' in out.raw[:n]  # escaped


@pytest.mark.parametrize('form,said', [
    (b'repo=gitlab.com', "That isn't a GitHub repo's link"),
    (b'repo=me/Q2-SPOTIFY', 'me/q2-spotify can\'t be an app here'),  # Q2 Pod's own id, another repo's name
    (b'repo=me/q2-photos', "me/q2-photos can't be an app here"),  # a firmware folder
    (b'repo=me/q2-a-name-too-longer', "me/q2-a-name-too-longer can't be an app here"),  # 16 bytes
    (b'repo=ME/Q2-FOO', 'me/q2-foo is in the App Store already, as Foo'),
    (b'repo=you/q2-foo', 'The App Store has Foo (me/q2-foo) already, whose card folder .foo this one would share'),
])
def test_what_cant_be_added_says_why_without_asking_github(fw, form, said):
    p = Phone(fw, {LIST: b'me/q2-foo Foo\n'})
    assert p.ask(b'POST /add', form)[0] == 303
    assert said in p.ask(b'GET /')[2] and MINE not in p.card.files and p.net.urls == [] and p.ph.seq == 0


@pytest.mark.parametrize('reply,said', [
    ((0, 404, None), 'GitHub shows no release of me/q2-bar (or no public repo of that name)'),
    ((0, 403, None), 'GitHub is busy: try again in a while'),
    ((7, 0, None), "Can't reach GitHub: is the Q2 still on Wi-Fi?"),
    ((0, 200, None), "me/q2-bar's latest release has no .zip of the app to install"),
])
def test_github_says_no(fw, reply, said):
    p = Phone(fw, releases={b'https://api.github.com/repos/me/q2-bar/': reply})
    p.card.files[LOOKUP] = github('1.0', ('src.tar.gz', 9))
    p.ask(b'POST /add', b'repo=me/q2-bar')
    assert said in p.ask(b'GET /')[2] and MINE not in p.card.files and p.ph.seq == 0


def test_a_full_store_takes_no_more(fw):
    p = Phone(fw, {LIST: b''.join(b'me/q2-a%d\n' % i for i in range(SH_MAX))})
    p.ask(b'POST /add', b'repo=me/q2-bar')
    assert 'The App Store is full (24 apps)' in p.ask(b'GET /')[2] and p.net.urls == []


def test_removing_drops_its_lines_and_the_file_once_empty(fw):
    p = Phone(fw, {MINE: b'me/q2-foo Foo\r\n\nme/q2-bar  Bar Two\nme/q2-foo Again\n'})
    assert p.ask(b'POST /remove', b'repo=Me%2Fq2-FOO')[0] == 303
    assert p.card.files[MINE] == b'me/q2-bar  Bar Two\n' and p.ph.seq == 1 and p.ph.note == b'Again removed'
    assert 'Removed Again from the App Store. Installed, it stays on the card' in p.ask(b'GET /')[2]
    p.ask(b'POST /remove', b'repo=me/q2-baz')
    assert "me/q2-baz isn't on your list" in p.ask(b'GET /')[2] and p.ph.seq == 1
    p.ask(b'POST /remove', b'repo=me/q2-bar')
    assert MINE not in p.card.files and p.ph.seq == 2 and 'Removed Bar Two' in p.ask(b'GET /')[2]


def test_the_state_file_holds_the_address_pin_and_code(fw):
    p = Phone(fw)
    def ioctl(fd, request, ifr, *_):
        assert request == 0x8915 and C.string_at(ifr) == b'wlan0'
        C.memmove(ifr + 16, bytes([2, 0, 0, 0, 192, 168, 1, 23]), 8)
    fw.mock('fw_ioctl', ioctl)
    p.ph.note = b'Paste an app\'s GitHub link there'
    fw.fn('shelf_phone__ph_state', None, C.c_int)(1)
    lines = dict(line.split(b'=', 1) for line in p.card.files[STATE].splitlines())
    size, hex_ = qr(fw, b'http://192.168.1.23:8300/?k=' + KEY)
    assert lines == {b'addr': b'192.168.1.23:8300', b'pin': PIN, b'qr': hex_.encode(), b'qrsize': b'%d' % size,
                     b'note': b"Paste an app's GitHub link there"} and size == 33
    p.card.files.pop(STATE)
    fw.fn('shelf_phone__ph_state', None, C.c_int)(0)
    assert STATE not in p.card.files  # the same address: nothing written
    fw.mock('fw_ioctl', lambda *_: -1)
    fw.fn('shelf_phone__ph_state', None, C.c_int)(0)
    assert p.card.files[STATE] == b'addr=\npin=548001\nqr=\nqrsize=0\nnote=Paste an app\'s GitHub link there\n'


def test_the_app_store_lists_the_added_after_its_own_and_keeps_one_removed_while_installed(fw):
    page = Page(fw, b'me/q2-foo Foo')
    page.card.files.update({LIST: b'me/q2-foo Foo\n', MINE: b'me/q2-bar Bar\nyou/q2-foo Dup\n'})
    assert fw.fn('shelf_reload', C.c_int)() == 1
    table = (App * SH_MAX).in_dll(fw.lib, 'shelf_apps')
    assert [table[i].name for i in range(fw.var('shelf_n', C.c_int).value)] == [b'Foo', b'Bar']  # a repeated id: the first
    assert page.sh.rows == 4 and page.sh.map[3] == ROW_ADD
    page.card.files.update({b'/mnt/mmc/.q2/apps/bar.tag': b'1.0'})
    page.card.files.pop(MINE)
    fw.fn('shelf_reload', C.c_int)()
    assert [table[i].name for i in range(fw.var('shelf_n', C.c_int).value)] == [b'Foo', b'Bar']  # installed: removable
    page.card.files.pop(b'/mnt/mmc/.q2/apps/bar.tag')
    fw.fn('shelf_reload', C.c_int)()
    assert fw.var('shelf_n', C.c_int).value == 1
    page.sh.job = 2
    page.card.files[MINE] = b'me/q2-bar Bar\n'
    assert fw.fn('shelf_reload', C.c_int)() == 0 and fw.var('shelf_n', C.c_int).value == 1  # a job's app is a row
    page.sh.job = 0
    page.card.files.pop(LIST)
    fw.fn('shelf_reload', C.c_int)()  # no list on the card: the page's own kept
    assert [table[i].name for i in range(fw.var('shelf_n', C.c_int).value)] == [b'Foo', b'Bar']


def test_a_check_keeps_the_added_off_the_cards_copy_of_the_list(fw):
    card = Card(fw, {MINE: b'me/q2-bar Bar\n'})
    apps(fw, b'me/foo')
    Net(fw, {b'https://raw': (0, 200, None), b'https://api': (0, 404, None)})
    card.files[b'/tmp/q2-shelf.json'] = b'me/foo Foo\n'
    sh = fw.var('netupdate__sh', Sh)
    fw.fn('netupdate__check', C.c_int)()
    assert [sh.next[i].id for i in range(sh.n)] == [b'foo', b'bar']
    assert card.written[LIST + b'.tmp'] == b'me/foo Foo\n'


def test_the_row_opens_the_page_and_its_end_reads_the_list_again(fw):
    page = Page(fw, b'me/q2-foo Foo')
    page.card.files[LIST] = b'me/q2-foo Foo\n'
    calls, timers, threads = [], [], []
    for stub in ('fw_socket', 'fw_setsockopt', 'fw_bind', 'fw_listen'):
        fw.mock(stub, lambda *a, stub=stub: calls.append((stub, *a[:3])) or (5 if stub == 'fw_socket' else 0))
    fw.mock('fw_bind', lambda fd, addr, *_: calls.append(('bind', C.string_at(addr, 4))) or (len(calls) < 4) * -1)
    fw.mock('fw_pthread_create', lambda t, a, fn, *_: threads.append(fn) or 0)
    fw.mock('timer_add', lambda fn, ctx, ms, *_: timers.append((fn, ms)) or 9)
    page.click(page.sh.rows - 1)  # Add an App
    ph = fw.var('shelf_phone__ph', Ph)
    assert ph.running == 1 and ph.port == 8301 and len(ph.key) == 16 and len(ph.pin) == 6 and ph.pin.isdigit()
    assert [c for c in calls if c[0] == 'bind'] == [('bind', b'\x02\x00\x20\x6c'), ('bind', b'\x02\x00\x20\x6d')]
    assert ('fw_listen', 5, 4, 0) in calls and len(threads) == 1 and timers[-1][1] == 500 and ph.stop == 0
    assert fw.fn('netupdate_busy', C.c_int)() == 1  # Wi-Fi kept on while it runs
    tick = C.CFUNCTYPE(C.c_int, C.c_void_p)(timers[-1][0])
    page.card.files[MINE] = b'me/q2-bar Bar\n'
    ph.seq += 1
    assert tick(None) == 8 and fw.var('shelf_n', C.c_int).value == 1  # not under the QR page
    fw.fn('addon__qr_closed', C.c_int, C.c_void_p, C.c_void_p)(None, None)  # Back
    assert ph.stop == 1 and ph.xfer[0] == 1
    assert tick(None) == 8 and fw.var('shelf_n', C.c_int).value == 2 and page.sh.rows == 4
    ph.running = 0
    assert tick(None) == 0 and ph.timer == 0 and fw.fn('netupdate_busy', C.c_int)() == 0


def test_no_port_says_so(fw):
    page = Page(fw, b'me/q2-foo Foo')
    closed = []
    fw.mock('fw_socket', lambda *_: 5)
    fw.mock('fw_bind', lambda *_: -1)
    fw.mock('fw_close', lambda fd, *_: closed.append(fd))
    assert fw.fn('phone_open', C.c_int)() == 0 and fw.var('shelf_phone__ph', Ph).running == 0
    assert page.toasts == ['Can\'t open the page for the phone'] and closed == [5]
    fw.var('shelf_phone__ph', Ph).running = 1  # the last one still ending
    assert fw.fn('phone_open', C.c_int)() == 0 and page.toasts[-1] == 'Wait a moment'
