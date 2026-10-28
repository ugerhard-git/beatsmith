# Security Policy

Beatsmith is a drum step sequencer plugin (VST3, Windows) that sends MIDI
into its host's track. It is a small personal open-source project, but it
runs inside other people's music software, so security and stability bugs
are taken seriously.

## Reporting a vulnerability

**Please do not open a public issue for security problems.**

Use GitHub's private reporting instead:

1. Go to the repository's **Security** tab.
2. Click **Report a vulnerability**.
3. Describe the problem, how to reproduce it (a preset or session file that
   triggers it is ideal), and the Beatsmith version and host (DAW) you used.

You can expect a first response within 14 days. Once a fix is released, the
advisory is published and you will be credited unless you prefer otherwise.

## Supported versions

Only the **latest release** receives security fixes. There are no long-term
support branches.

## Threat model

What we consider in scope:

- **Untrusted input files.** Preset files, imported patterns and kits, and
  exported or imported MIDI files may come from anyone. Malformed or
  malicious files must never crash the host, corrupt memory, hang the
  plugin, or write outside the user's chosen folders.
- **Host session state.** The state blob a DAW hands back to the plugin is
  treated as untrusted too: it may be from an older or newer Beatsmith
  version, truncated, or tampered with.
- **Supply chain.** The build pipeline, third-party GitHub Actions and
  pinned dependencies (JUCE, which bundles the VST3 SDK).

What we consider out of scope:

- Attacks that require an already compromised computer or DAW.
- Windows SmartScreen warnings on unsigned builds (code signing is a
  planned future improvement, see `docs/IDEAS.md`).

## Security commitments

The plugin itself:

- makes **no network connections**: no telemetry, no update checks, no
  online content;
- only reads and writes files in locations the user picks or in its own
  preset folder inside the user's Documents folder;
- validates every loaded file and state blob (size limits, version fields,
  bounds checks on every count and index) before using it;
- never allocates memory or takes locks on the real-time audio thread, so a
  bad input cannot stall the host's audio engine.

The repository and its builds:

- every change to `main` goes through a pull request and must pass CI;
- third-party GitHub Actions are pinned to exact commit SHAs and kept up to
  date by Dependabot;
- workflows run with read-only permissions by default; write access is
  granted only to the specific release job that needs it;
- dependencies are pinned to exact versions, never "latest";
- releases are built only by GitHub Actions, never on a personal machine,
  and ship with SHA-256 checksums and build-provenance attestations.

## Verifying a release

Each release has a `SHA256SUMS.txt` with the checksum of every download. On Windows:

```powershell
Get-FileHash .\Beatsmith-Windows-v<version>.zip -Algorithm SHA256
```

Compare the result with the checksum on the release page. With the
[GitHub CLI](https://cli.github.com/) you can also confirm the file was
built by this repository's CI:

```powershell
gh attestation verify .\Beatsmith-Windows-v<version>.zip --repo ugerhard-git/beatsmith
```
