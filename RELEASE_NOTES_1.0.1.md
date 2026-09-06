# Modbus TCP Client 1.0.1

Discovery now uses a stable engine notification ID and declares engine mute support. The finder observes persisted mute state through the SDK's on-mute/on-unmute handle, stops probing and announcing muted endpoints, and resumes discovery after unmute. A second check after register scanning prevents a mute or module creation during a scan from generating a stale offer.

Configured endpoints are checked by their live connection parameters, including renamed modules. The plugin retains pending request IDs, and owns the decision to reopen discovery after a module has been removed. DARTWIC provides the notification framework, discovery UI, and provisioning actions.

Validation covers repeated announcements, mute/unmute callbacks, persisted mute at startup, a mute during scanning, preference lookup failure, and configured/removed devices. The native read/write and register-scan integration suites pass.

A live simulator test against the existing debug Engine also confirmed that a saved mute prevents network probing, unmute creates an engine-mutable discovery request, request-level mute remains stable across repeated polls with no new probes/scans, and a second unmute resumes discovery. Temporary discovery settings were restored afterward.

## Release status

This package is a draft pending a compatible DARTWIC Engine release (minimum beta.4). Published Engine beta.3 does not expose the required `isNotificationMuted` SDK method; current development builds do. Do not publish the draft until the compatible Engine is released. Pair it with the interface notification-state fixes from the DARTWIC development branch to prevent stale polling from overriding mutes.

The archive includes release/debug Windows engine plugins and the unchanged interface plugin payload. No DARTWIC Engine or Interface application builds are included.
