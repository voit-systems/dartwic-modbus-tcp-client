# Modbus TCP Client 1.0.1

Discovery now uses a stable engine notification ID and declares engine mute support. The finder observes persisted mute state through the SDK's on-mute/on-unmute handle, stops probing and announcing muted endpoints, and resumes discovery after unmute. A second check after register scanning prevents a mute or module creation during a scan from generating a stale offer.

Configured endpoints are checked by their live connection parameters, including renamed modules. The plugin retains pending request IDs, and owns the decision to reopen discovery after a module has been removed. DARTWIC provides the notification framework, discovery UI, and provisioning actions.

Pending offers are now revalidated every five seconds using actual Modbus reads on the responding unit, not only a TCP connection. Each offer carries a 15-second presence lease. Failed verification or mute withdraws availability; successful verification renews the existing offer without repeating the popup. The discovery UI expires stale leases even if an update is missed, and checks current availability before provisioning. A saved mute is displayed separately from an active device offer. Unmuting an offline device cannot resurrect it.

Validation covers repeated announcements, mute/unmute callbacks, persisted mute at startup, a mute during scanning, preference lookup failure, and configured/removed devices. The native read/write and register-scan integration suites pass.

A live simulator test against the existing debug Engine also confirmed that a saved mute prevents network probing, unmute creates an engine-mutable discovery request, request-level mute remains stable across repeated polls with no new probes/scans, and a second unmute resumes discovery. Temporary discovery settings were restored afterward.

The shutdown/restart regression was also tested live: stopping the simulator withdrew its offer in approximately six seconds. Muting and unmuting while offline did not restore the offer; restarting the simulator triggered fresh discovery. The UI lease-expiry and inactive saved-mute tests pass alongside the native lifecycle tests.

## Release status

This package is a draft pending compatible DARTWIC Engine and Interface releases (minimum beta.4). Published Engine beta.3 does not expose the required `isNotificationMuted` SDK method; current development builds do. The Interface must include the discovery presence-lease and notification-state fixes. Do not publish the draft until both compatible applications are released. These application changes remain local and are not included in this release.

The archive includes release/debug Windows engine plugins and the unchanged interface plugin payload. No DARTWIC Engine or Interface application builds are included.
