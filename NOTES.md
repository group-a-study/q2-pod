Test build of Q2 Pod 1.7.6 from `53275e6b840eff91af96c79d0aaef427fe9a162f` (53275e6b), built 2026-11-11. Not an official release; those are at https://github.com/DiamondBond/q2-pod/releases.

- **update.tar**: iPod edition, V027i (About shows 1.7.6 Pod dev 79)
- No Legacy edition: test builds are iPod only.

**Install:** on a Q2 already running a test build from this fork, **System settings → System Update → Network Update**. Otherwise copy `update.tar` to the root of the card, then **TF card update**. Each test build installs over any other build.

Network Update in these builds fetches this fork's releases (group-a-study/q2-pod), not DiamondBond's.

The App Store lists these apps too, after Q2 Pod's own list: group-a-study/q2-applemusic Apple Music, group-a-study/q2-airplay AirPlay, group-a-study/q2-bandcamp Bandcamp.

**Changes not yet in DiamondBond/q2-pod main:**
- apps: Add an App adds a GitHub repo to the App Store from a phone; build.py --extra-app (53275e6b)
- ipod: Classic's AirPlay 3 px right, the row 25 px apart, glyph to glyph (fork-only) (a48a5e87)
- ipod: Classic's icon row 24 px apart, glyph to glyph, AirPlay where it is (fork-only) (05a60545)
- ipod: Classic's heart at Minimal's spacing from order, AirPlay as dev74 (fork-only) (75d81795)
- Merge remote-tracking branch 'upstream/main' into test-1.7 (c4ebd836)
- library: a list in name order goes a row a tick until its letters, as Cover Flow (fork-only) (c38469e4)
- ipod: Classic's icon row at Minimal's spacing again, order where it is (fork-only) (42e1f45e)
- library: letters in every list in name order, started by the wheel's own clock (fork-only) (c0aef240)
- ipod: Minimal's icon row back where it was; Classic's row closer, its dots at the foot (fork-only) (5851e689)
- Revert "status bar: the percentage alone 3 px further from Wi-Fi (fork-only)" (8297a26e)
- coverflow: the wheel a tick a cover again, letters held under the finger (fork-only) (d4c91651)
- library: letters stay while the finger stays on the wheel, as an iPod classic's (fork-only) (deba2adb)
- classic: Now Playing's art upright over its reflection (fork-only) (1de6197e)
- status bar: the percentage alone 3 px further from Wi-Fi (fork-only) (70434051)
- ipod: Minimal's page dots under the progress bar, between the times (fork-only) (9990d146)
- ipod: Now Playing's icon row 16 px higher; Classic's where Minimal's is (fork-only) (f910fc8d)
- home: the pane's tests measure from the panel, wherever the still art's box is (fork-only) (42116f0f)
- home: the pan's tests run only with HOME_PAN on (fork-only) (b608ce7b)
- lyrics: the page's tests expect Inter and the swipe's dropped hold (fork-only) (513952ec)
- lyrics: the report's test expects stock's jump off the page (fork-only) (4e0a7997)
- library: letters judge a table again when its records change (5f25fa58)
- format: clang-format the fork's letters and scan stub (74cc2e4a)
- library: a fast spin goes by letters in name-ordered lists (fork-only) (6ce24839)
- lyrics: the tests expect a swipe away to drop the hold (fork-only) (0c68fea0)
- dev: the tests expect no wheel log in any build (fork-only) (d622bfbf)
- lyrics: the host tests know the Inter face (fork-only) (a74e0f02)
- AirPlay: the host tests know its app field and More row (fork-only) (8329f079)
- lyrics: a swipe off the lyrics page drops the hold again (fork-only) (05280896)
- home: Split's art stays still (fork-only) (3bd81510)
- dev: no debug logs in --dev builds (fork-only) (a18653b5)
- library: an artist's albums show every spelling of its name (fork-only) (dfff58aa)
- library: Update Local Music skips a folder holding .nomedia (fork-only) (2dc7c78b)
- coverflow: a playlist kept as an album shows the playlist's cover (fork-only) (bcce51ee)
- lyrics: Inter Display ExtraBold again on the lyrics page (fork-only) (e507fa31)
- AirPlay: the speaker's volume and the earphones' kept apart (4cc10246)
- AirPlay: play what the Q2 plays on an AirPlay speaker (fork-only) (77b3c46c)

**SHA-256:**
```
1d24da016cbeb495f7e2eafac61b01e86c879b436906fe167448f96151a9bcb7  update.tar
```
