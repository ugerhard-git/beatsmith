# Ideas and postponed features

Features and improvements that have been discussed but postponed, so they
are not forgotten. Agreed designs are marked *Planned*, open ideas *Idea*. Move an item into
`PLAN.md` once it is scheduled, and remove it here once it is built.

## Security and distribution

- *Idea* – **Code signing of Windows releases.** Unsigned plugins trigger Windows
  SmartScreen warnings. Signing needs a paid certificate (or a signing
  service) and a signing secret in CI, so it is postponed until releases
  are shared more widely.

## Programming

- *Idea* – **Live recording into the grid.** Notes played on a keyboard (or coming from a MIDI clip) while
  the host plays could be written into the lane with that note, quantised to the lane's steps. Until then,
  incoming MIDI only passes through.
