Test build of Q2 Pod 1.4.4 from `album-lyrics` (16189b7), built 2026-10-25. Not an official release; those are at https://github.com/DiamondBond/q2-pod/releases.

- **update.tar**: iPod edition, V00Fi (About shows 1.4.4 Pod dev 15)
- **update-legacy.tar**: Legacy edition, V00Gs (About shows 1.4.4 dev 16). Rename it to `update.tar` before you copy it to the card.

**Install:** on a Q2 already running a test build from this fork, **System settings → System Update → Network Update**. Otherwise copy `update.tar` to the root of the card, then **TF card update**. Each test build installs over any other build.

Network Update in these builds fetches this fork's releases (group-a-study/q2-pod), not DiamondBond's.

**Changes not yet in DiamondBond/q2-pod main:**
- lyrics: Download Album Lyrics on Now Playing's ⋯ page (16189b7)
- lyrics: LRCLIB's match for the track's own length (ec3e34f)

**SHA-256:**
```
48c829b18aa30cdf864c18af40f0b71e9240b408f0b9a627a155c164d6ea696c  update.tar
6b84b878ea8009513ba87006a11daa148cea5f8a97b47c299c9d1c368d166330  update-legacy.tar
```
