<p align="right"><strong>English</strong> · <a href="suishen-xiaoqian-publish.zh_CN.md">简体中文</a></p>

# Suishen Xiaoqian publication checkpoint

This is a development checkpoint, not a published release. Keep the verified
personal installation on the device while preparing a public version.

## Current deliverables

- Four-entry AI Passport firmware with a QR-linked phone companion.
- ESP32-C3 application image: 1,466,688 bytes. The full image is verified for
  a standalone flash at `0x0`; the application image is the candidate for a
  MetaPass2 child slot, subject to that installer's size and device checks.
- The source default for `CONFIG_XIAOQIAN_COMPANION_URL` is empty. A blank URL
  displays a configuration message instead of a QR code. An already flashed
  personal image keeps its existing URL.
- When running from a MetaPass OTA slot, long-press DOWN on the home screen to
  select the factory launcher and restart. In a standalone factory installation,
  that gesture does nothing. No MetaPass device installation has been tested.

## Before a public community submission

1. Choose a public HTTPS home for the phone companion, or document a
   self-hosted-only edition. The current LAN server cannot serve other users.
2. Decide how other users supply AI credentials. Do not publish the developer's
   API key, a server with unrestricted use of that key, or a local IP in a
   public firmware. Add cost controls and request limits before public hosting.
3. Build a release image with the intended companion URL, then run the full
   validation gate and test all four QR entries on a separate device/network.
4. Install that exact image in a MetaPass2 slot and check boot, long-DOWN return,
   Wi-Fi, calendar, all buttons, QR, and phone interpretation. A normal full
   flash of MetaPass2 replaces the current standalone firmware and can reset
   saved Wi-Fi settings; obtain approval for that specific write.
5. Public source may first live in a personal GitHub repository with self-hosting
   instructions. Before submitting an installable community play, review the
   firmware, screenshots, third-party licenses, and public description.

The offline single-player game is a separate future application. Its controls,
visual style, and size must be specified and tested before a second slot is
installed. MetaPass2's advertised slot count is a maximum, not a guarantee that
any two arbitrary images will fit or work together.
