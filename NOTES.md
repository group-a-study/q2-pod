Test build of Q2 Pod 1.5.3 from `f1dfc41185ca55c0f051e3e6256f56cd256d4f21` (f1dfc41), built 2026-10-29. Not an official release; those are at https://github.com/DiamondBond/q2-pod/releases.

- **update.tar**: iPod edition, V01Vi (About shows 1.5.3 Pod dev 67)
- No Legacy edition: test builds are iPod only.

**Install:** on a Q2 already running a test build from this fork, **System settings → System Update → Network Update**. Otherwise copy `update.tar` to the root of the card, then **TF card update**. Each test build installs over any other build.

Network Update in these builds fetches this fork's releases (group-a-study/q2-pod), not DiamondBond's.

**Changes not yet in DiamondBond/q2-pod main:**
- dev: no console log in --dev builds (fork-only) (f1dfc41)
- dev: no q2trace (reverts 1d89bdc) (afa7c2b)
- dev: q2trace, a playback trace on the card (docs/remote.md#trace) (1d89bdc)
- Low Power: both cores stay online while an AirPlay speaker plays (1c428c9)
- AirPlay: the speaker's volume and the earphones' kept apart (d2e21c0)
- lyrics: Inter Display ExtraBold again on the lyrics page (fork-only) (3b12a4b)
- ipod: Now Playing's icon row back where it was before fdcc978 (550fbcc)
- ipod: Now Playing's icon row 16px above the art's foot (37b32f9)
- ipod: Now Playing's AirPlay, order and favourite icons 8px higher (71e01d7)
- AirPlay: play what the Q2 plays on an AirPlay speaker (fork-only) (f8f6cdb)

**SHA-256:**
```
2f2fafe623e0cafff2c4713db01138e621626706d79ae40faedc0016366e8fcb  update.tar
```
