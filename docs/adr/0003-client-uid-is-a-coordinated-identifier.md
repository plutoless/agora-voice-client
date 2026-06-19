# The Client UID is an Operator-chosen, coordinated identifier

The AI Agent subscribes to the Client's audio by uid, so the Client cannot use uid `0`
(auto-assign) on the real path — the AI could not predict the value. Instead the Operator
picks a fixed uid via `--uid`, mints the token bound to that uid, joins with it, and
separately configures the AI Agent to subscribe to the same number. The uid is therefore
an integration contract coordinated out-of-band between two independent systems, not an
internal detail.

Consequences:
- `--uid` is effectively required for talking to the AI; the `0` default exists only for
  solo/echo testing where nobody subscribes to the Client.
- The resolved uid is a single value flowing to both token minting and channel join, so
  the two cannot drift (a mismatch makes Agora reject the join).
