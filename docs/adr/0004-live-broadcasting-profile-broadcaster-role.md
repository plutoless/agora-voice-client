# Channel profile LIVE_BROADCASTING with role BROADCASTER

The Client joins with channel profile `LIVE_BROADCASTING` and always sets client role to
`BROADCASTER`. For a 1:1 conversation the intuitive choice would be `COMMUNICATION`, but
`LIVE_BROADCASTING` is Agora's recommended profile in the 4.x SDK, and all participants
in a channel must share the same profile (including the AI Agent).

The non-obvious trap this records: `LIVE_BROADCASTING` has a broadcaster/audience split,
and the default role does not publish the microphone. The Client must therefore call
`setClientRole(BROADCASTER)` explicitly — otherwise everything looks connected but the AI
Agent hears silence. The Client is never AUDIENCE; it always publishes its mic, so the
role is fixed, not configurable.
