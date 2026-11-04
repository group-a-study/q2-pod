Test build of Q2 Pod 1.5.3 from `1deb077fa5fff70888c1d541b6b5b277e5054806` (1deb077), built 2026-11-04. Not an official release; those are at https://github.com/DiamondBond/q2-pod/releases.

- **update.tar**: iPod edition, V01Zi (About shows 1.5.3 Pod dev 71)
- No Legacy edition: test builds are iPod only.

**Install:** on a Q2 already running a test build from this fork, **System settings → System Update → Network Update**. Otherwise copy `update.tar` to the root of the card, then **TF card update**. Each test build installs over any other build.

Network Update in these builds fetches this fork's releases (group-a-study/q2-pod), not DiamondBond's.

**Changes not yet in DiamondBond/q2-pod main:**
- library: an artist's albums show every spelling of its name (1deb077)
- library: Update Local Music skips a folder holding .nomedia (85abae2)
- test: the grid cover machine formats playlist_album's path (d82e74d)
- coverflow: a playlist kept as an album shows the playlist's cover (fec11e4)
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
b4eb15338238e6f477a46dabb41a67220b28bdbe5eddb493a75076e35f950cab  update.tar
```
